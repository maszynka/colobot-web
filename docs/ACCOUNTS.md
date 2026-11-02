# Accounts and cloud saves

Players choose one of three modes the first time they open the game:

| mode | where saves live | sync |
| --- | --- | --- |
| Sign in with Google | IndexedDB (this browser) + R2 | automatic; continue on any device |
| Email + password | IndexedDB + R2 | automatic; continue on any device |
| Play locally | IndexedDB only | none. "Download saves" makes a `.zip` backup |

All modes can **Download saves** (zip of `/save`) and **Import saves** (zip, replaces `/save`, reloads) from the account badge in the top-right corner. Local players also get a "Saved · Download" toast after a game save (`data.sav` changed, at most every 2 min). The UI is English, or Polish when `navigator.language` starts with `pl`.

## Architecture

```
browser                                     Cloudflare Worker "colobot-web"
 loader.js ── startAccount() (account.js)    /api/*  -> worker/index.js (run_worker_first)
   chooser overlay / badge / menu            other   -> static assets (dist/, ASSETS binding)
   Module.onSaveMounted  <─ web_archiver.c   D1  DB     users, sessions, oauth_identities, saves, rate_limits
   Module.onSaveFlushed  <─ web_archiver.c   R2  SAVES  u/<user>/<version>-<rand>.zip
   zip.js (deflate-raw writer/reader)
```

- **Mount order.** `web_archiver.c` mounts IDBFS on `/save`, loads it (`FS.syncfs(true)`), then awaits `Module.onSaveMounted()` before `main()` reads any save (JSPI keeps `main` suspended). `account.js` waits there until the player has picked a mode, compares local and cloud and, if needed, replaces `/save` from the cloud archive and flushes it to IndexedDB. The sign-in overlay runs while the wasm and data download, so it costs no extra load time.
- **Upload.** After every completed IDBFS flush, `web_archiver.c` calls `Module.onSaveFlushed()`. In cloud mode, `account.js` debounces for 5 s, flushes `/save` to IndexedDB once more (so IndexedDB never lags behind what the cloud gets), zips `/save` (files plus empty directories, because an empty `savegame/<player>/` is a player profile), and skips the upload if the content hash (SHA-256 over sorted path + bytes) is unchanged. Otherwise it sends `PUT /api/saves` with `X-Base-Version`. It also uploads when the tab is hidden while an upload is pending, and when the API is reachable again.
- **What is synced.** Everything under `/save` except `colobot.json`, the per-device settings file (resolution, key bindings, audio). It is left out of the archive and the hash, and a restore keeps this device's copy, so two devices with different settings never see a false conflict. "Download saves" still includes it.
- **Per-device sync state** lives in `localStorage['colobot.sync.<userId>'] = {base, hash}`: the cloud version this device last synced with and the content hash at that point. It is re-read before every upload. Also in `localStorage`: `colobot.mode` (chosen mode), `colobot.user` (last signed-in user, for offline starts) and `colobot.owner` (the user id whose saves `/save` holds).
- **Restore order.** A restore writes `/save`, flushes it to IndexedDB, and only then records `{base, hash}`. If the flush fails or the tab dies in between, the old state stays and the next start shows the conflict dialog instead of uploading the stale files over the newer cloud version.
- **One tab syncs.** Each tab has its own in-memory copy of `/save`. The first tab takes the Web Lock `colobot-save` for its lifetime; later tabs show "open in another tab" and do not sync.
- **Shared browser.** If `/save` holds saves last synced with a different account (`colobot.owner`), signing in asks "Upload to this account" or "Start fresh" (clears `/save` here; the other account keeps its cloud copy) before anything is uploaded. Saves from local play (no owner) are uploaded to the first account without asking: that is the "sign in to play on many devices" path. Sign-out asks whether to keep the saves in this browser or remove them (with a warning if the last upload failed).
- **Reconcile at start (cloud mode).** The client compares the cloud `latest` with this device's state:
  - no cloud save: upload the local saves if there are any;
  - same content hash: record the version and do nothing else;
  - `base == latest.version`: only this device changed, so upload;
  - the local saves are empty, or unchanged since the last sync: restore from the cloud;
  - otherwise it is a **conflict**. A dialog shows the cloud version (number, device, time) with "Download cloud version" and "Download this device's version", then "Use cloud" (restore) or "Use this device" (upload as a new version on top). Neither copy is lost: you can download both first, and the server keeps the last 10 versions.
- **Conflict during play.** A `409` from `PUT` opens the same dialog. "Use cloud" restores and reloads the page.
- **Offline / API unreachable.** A player who was signed in stays in cloud mode for the remembered user (badge "· offline"). Changes stay in IndexedDB and are pending. `account.js` retries `/api/me` with backoff (15 s doubling to 5 min, and on the browser's `online` event); when the API answers, it runs the start-up reconcile (or uploads the pending change). If the cloud got newer saves meanwhile and nothing changed here, a toast offers "Reload". Without a remembered user, or with no account at all, the game plays locally with no prompt.
- **Static hosting without the Worker** (e.g. `tests/e2e/boot.mjs`): `/api/me` fails, so the game runs in local mode.

## API

All responses are JSON with `Cache-Control: no-store`, except archive downloads. On errors the body is `{"error": code, ...}`.

| method | path | auth | notes |
| --- | --- | --- | --- |
| GET | `/api/me` | – | `{user: {id,email,provider}|null, google: bool}` (`google` = OAuth configured) |
| DELETE | `/api/me` | session + re-auth | body `{confirm: "delete", password}` (password accounts) or `{confirm: "delete"}` with a session younger than 10 min (Google: `403 reauth_required` otherwise). Deletes the user, sessions, identities, rate-limit rows, every save row and R2 object; clears the cookie |
| POST | `/api/auth/register` | – | `{email, password}` (8–200 chars) → session cookie. `409 email_taken` |
| POST | `/api/auth/login` | – | `{email, password}` → session cookie. `401 bad_credentials`, `429 too_many_attempts` (+`Retry-After`) |
| POST | `/api/auth/logout` | – | deletes the session row, clears the cookie |
| GET | `/api/auth/google/start` | – | 302 to Google (state + PKCE S256). Not configured → 302 `/?auth_error=google_not_configured` |
| GET | `/api/auth/google/callback` | – | checks state, exchanges code + verifier, checks `id_token` aud/iss/exp, creates or loads the user → 302 `/?signed_in=google` or `/?auth_error=…` |
| GET | `/api/saves` | session | `{latest, versions: [{version,size,hash,device,createdAt}], maxBytes}` |
| GET | `/api/saves/latest`, `/api/saves/<n>` | session | `application/zip`, headers `X-Save-Version`, `X-Save-Hash` |
| PUT | `/api/saves` | session | body = zip (≤ 20 MB). Headers: `X-Base-Version` (0 = none), `X-Save-Hash`, `X-Device`. `409 conflict {latest}` if the cloud moved past the base; `413 too_large`; `429` past 60 uploads per 15 min. Keeps the newest 10 versions |

### Security

- **Passwords.** PBKDF2-SHA256, 20 000 iterations by default, 16-byte random salt per user, compared in constant time (`crypto.subtle.timingSafeEqual`, a Workers extension, with a JS fallback). The iteration count is stored per user; the var `PBKDF2_ITER` (10 000–100 000, Workers' cap) changes it, and the next successful login re-hashes at the new count. Unknown emails run a dummy derivation, so response timing does not reveal which accounts exist. See "Password hashing cost" below.
- **Sessions.** The token is 32 random bytes in cookie `__Host-colobot_session` (`HttpOnly; Secure; SameSite=Lax; Path=/`, 30 days). A malformed cookie value is ignored (treated as signed out). D1 stores only its SHA-256. Logout deletes the row, and expired rows are pruned whenever a new session is created.
- **CSRF.** Every non-GET request must carry `Origin` equal to the request's own origin (or, when a browser omits `Origin`, `Sec-Fetch-Site: same-origin`). Do not add `Referrer-Policy: no-referrer` to the pages: it makes `Origin` `null`. A POST must be `application/json`. A PUT or DELETE must send `X-Colobot: 1`, a header a cross-site form cannot set.
- **Rate limits** (D1 `rate_limits`). Failed logins: 30 per IP per 15 min, and per (IP, email) the first 5 failures are free, then each attempt waits 2, 4, 8 … s (capped at 15 min) after the last failure; 15 quiet minutes reset it. Nothing is keyed by the email alone, so nobody can lock a victim out by knowing their address (the price: a botnet spread over many IPs is slowed only per IP; Turnstile on the login form is the next step if that is ever seen). Registrations: 10 per IP per 15 min. Uploads: 60 per account per 15 min. Expired rows are pruned on about 1 in 16 writes (index on `window_start`, migration 0002).
- **Google OAuth.** Authorization-code flow with `state` and PKCE (S256), scope `openid email`. The state and verifier sit in a 10-minute HttpOnly cookie scoped to `/api/auth/google`. Google accounts are **not** auto-linked to an email/password account with the same address: that would let someone pre-register a victim's email and later read their saves. Each Google identity gets its own user.
- **Request bodies.** Read as a stream and cut off past the limit (16 KB JSON, 20 MB zip), so a chunked body without `Content-Length` cannot make the Worker buffer more.
- **Zip reader** (`web/zip.js`, client). At most 5000 entries, 32 MB per entry and 64 MB in total when unpacked; the inflate stream is counted and cancelled past the declared size, so a zip bomb (cloud or imported) cannot exhaust the tab's memory.
- **Uploads.** Must start with a zip signature and be at most 20 MB. R2 keys are random per version, and concurrent uploads race on the `(user_id, version)` primary key, so the loser gets a `409`.

## Setup

Resources already exist in the account (ids in `wrangler.jsonc`): D1 `colobot-web` and R2 bucket `colobot-web-saves`.

### Remote steps before the first deploy with the Worker

```sh
npx wrangler d1 migrations apply colobot-web --remote   # 0001 tables + 0002 rate-limit index (not applied yet)
npx wrangler deploy                                      # Worker + assets (after scripts/assemble-dist.sh)
curl -s https://colobot.codestiny.pl/api/me              # {"user":null,"google":false}
```

The `colobot.wasm` and `colobot.js` built from this commit include the `onSaveMounted` / `onSaveFlushed` hooks. An older wasm still runs, but without cloud sync (the hooks are never called).

### Google sign-in (needs your action)

The codestiny apps (`turbo-monorepo/apps/pro-platform`) already use a Google OAuth client. Its values exist only as Wrangler secrets, so you can either reuse that client or create a new one:

1. [Google Cloud Console → APIs & Services → Credentials](https://console.cloud.google.com/apis/credentials). Open the existing **Web application** OAuth client, or create one ("OAuth client ID" → Web application, name "Colobot Web").
2. **Authorized redirect URIs**: add `https://colobot.codestiny.pl/api/auth/google/callback`. For local testing, optionally add `http://localhost:8787/api/auth/google/callback`.
3. OAuth consent screen: app name "Colobot Web", support email, scopes `openid` and `email` (non-sensitive, no verification needed), and privacy policy URL `https://colobot.codestiny.pl/privacy.html`. Publish the app ("In production"), or only test users can sign in.
4. Set the secrets (values never go into the repo):
   ```sh
   npx wrangler secret put GOOGLE_CLIENT_ID
   npx wrangler secret put GOOGLE_CLIENT_SECRET
   ```
5. Check: `curl -s https://colobot.codestiny.pl/api/me` returns `"google":true`, and the chooser's Google button is enabled.

Until both secrets are set, the button reads "Google — not configured yet" and `/api/auth/google/start` redirects back with `auth_error=google_not_configured`. The callback URL is built as `<request origin>/api/auth/google/callback`, so sign-in through `*.workers.dev` works only if that URI is registered too. To pin one URI instead, set the var `GOOGLE_REDIRECT_URI`.

### Local development

```sh
npx wrangler d1 migrations apply colobot-web --local
npx wrangler dev                       # http://localhost:8787 (serves dist/ + /api)
# optional: .dev.vars with GOOGLE_CLIENT_ID=... GOOGLE_CLIENT_SECRET=... (gitignored)
node tests/e2e/accounts.mjs            # own wrangler dev on :8790 with a temp D1/R2
node tests/e2e/accounts-api.mjs        # API only (no browser), :8791
node tests/unit/zip-limits.mjs
```

`tests/e2e/accounts.mjs` covers:

- register, create a player, upload;
- "Download saves" yields a valid zip;
- a second browser signs in and gets the saves restored before the game reads them;
- a conflict prompt, then "Use this device" uploads v3;
- the session survives a reload;
- `colobot.json` is not in the cloud archive, and a settings-only change uploads nothing;
- a second tab is secondary (Web Lock);
- offline start (API blocked) stays in cloud mode, the change stays pending, and it is uploaded when the API is back;
- after "Sign out, keep saves here", a new account is asked before getting them; "Start fresh" uploads nothing;
- local mode never calls `/api/saves` and is remembered across reloads.

`tests/e2e/accounts-api.mjs` covers: 12 wrong passwords from one IP never lock the victim out on another IP, and give the attacker a short backoff (not a 15-minute lock); a malformed cookie gives `user: null` / `401`, not `500`; chunked bodies over the limit give `413`; the per-user upload limit; `DELETE /api/me` without confirmation or with a wrong password is refused.

## Limits / notes

- **Password hashing cost.** The Workers plan (Free: 10 ms CPU per request; Paid: 30 s) was not known when this was written, so the default must fit Free. Measured in workerd (`wrangler dev` 4.156, Apple M4 Pro; `Date.now()` advances during CPU work in local workerd), one `deriveBits` costs 0.098 ms per 1000 iterations: 10k = 0.95 ms, 20k = 1.95 ms, 50k = 4.9 ms, 100k = 9.8 ms (20 runs each, linear). Production cores can be 2–3× slower, and a login also hashes the session token and, after a change of count, re-hashes once. 20 000 iterations (≈ 2 ms here, ≈ 4–6 ms worst case there) leaves room under 10 ms; 100k would not. On Workers Paid, set `PBKDF2_ITER=100000` (Workers' maximum): new and next-login hashes use it. After a deploy, `wrangler tail` the first sign-ins and look for `Exceeded CPU Limit`.
- Saves are small (a profile plus missions is about 1–500 KB; each saved mission has a `screen.png`). The 20 MB cap is far above normal use.
- Signing out keeps the saves in this browser's IndexedDB. Clear site data to remove them.
