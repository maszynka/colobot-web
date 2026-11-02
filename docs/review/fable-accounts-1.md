# Review: accounts + cloud saves (fable-accounts-1)

Scope: `git diff e85f375~1 HEAD -- . ':!package-lock.json'` (commits `e85f375` … `9af1996`; also `.review/diff3.patch`),
read against `docs/ACCOUNTS.md`, `worker/index.js`, `worker/crypto.js`, `migrations/0001_accounts.sql`,
`web/account.js`, `web/zip.js`, `web/privacy.html`, `web/loader.js`, `web/sw.js`,
`src/colobot-base/src/common/resources/web_archiver.c`. Independent, read-only review; nothing but this
file was written. Every finding below was checked against the code, and several against a live
`wrangler dev` instance (curl probes) and the e2e suite.

What was run:

- `node tests/e2e/accounts.mjs`: **11/11 PASS** (register, upload, download zip, restore on a second
  browser, conflict prompt, "Use this device" uploads v3, session survives reload, local mode never
  touches `/api/saves`).
- `wrangler dev` probes: malformed cookie → `500` (F7); POST without `Origin` → `403 bad_origin`;
  chunked 25 MB PUT without `Content-Length` → `413` after the body was read; FK cascade on D1 local
  (`DELETE FROM users` removes `saves` and `sessions` rows, so `PRAGMA foreign_keys` is on); login
  timing for an existing vs unknown email: 23 ms vs 21 ms (dummy derivation works).
- Plan check: the wrangler OAuth token has no billing scope (`/accounts/:id/subscriptions` → auth
  error), so the Workers plan (Free vs Paid) **could not be determined**. The Worker and account use
  usage model `standard`, which exists on both plans. `colobot.codestiny.pl/api/me` is still `404`
  (Worker not deployed yet).
- Secret scan over the diff and `git ls-files`: no client secrets, tokens or keys committed.
  `.dev.vars`, `.wrangler/`, `.review/`, `tests/e2e/out/` are gitignored. `wrangler.jsonc` contains
  only the D1 database id (not secret).

## Findings

### Major

**F1 — PBKDF2 100k per login/registration vs the 10 ms CPU limit of Workers Free (plan unknown).**
`worker/crypto.js:3,47-62`, `worker/index.js:129,153`. One `deriveBits` with 100 000 iterations costs
~12 ms on an Apple M-series in Node; Workers isolates are typically slower, so 20–50 ms is realistic.
Every `/api/auth/login` (real or dummy derivation) and `/api/auth/register` does exactly one of them.
On the Free plan (10 ms CPU per request) this fails with error 1102 on most or all sign-ins; the feature
is then unusable for email accounts, while Google sign-in keeps working. `docs/ACCOUNTS.md` notes the
risk but the code has no guard. Fix: confirm the plan before `wrangler deploy` (dashboard → Workers &
Pages → Plans). If Free: either upgrade, or use `PBKDF2_ITER` ≈ 20 000 (still stored per user via
`password_iter`, so it can be raised later without migration) and keep `dummyVerify` at the same
count. In either case, tail the first production logins with `wrangler tail` and look for
`Exceeded CPU Limit`.

**F2 — `restoreFromCloud` records "synced" before the IndexedDB flush succeeds; a later start can
overwrite the newer cloud version with stale local data.** `web/account.js:351-357`:
`storeSync({base: latest.version, hash})` runs *before* `await s.Module.__wfsFlushNow()`. If the flush
fails or the tab is closed/killed between the two (the restore happens during loading, when users
close tabs), IndexedDB still holds the old `/save` while `localStorage` says "base = latest, hash =
cloud hash". Next start on this device: `localHash !== latest.hash`, `st.base === latest.version` →
branch "only this device changed" (`account.js:414`) → the old local saves are uploaded as a new
version on top of the newer cloud save, with no prompt. The server keeps 10 versions, so it is
recoverable by hand (`GET /api/saves/<n>`) but the UI offers no way back. Fix: `await __wfsFlushNow()`
first, then `storeSync(...)`; if the flush throws, leave the sync state untouched (the next start will
then show the conflict dialog instead of silently uploading).

**F3 — `colobot.json` (per-device settings) is part of the synced archive and of the content hash,
so two devices ping-pong conflicts.** `web/account.js:101-122` syncs everything under `/save`,
including `/save/colobot.json` (confirmed in the e2e download:
`["/save/colobot.json","/save/mods/","/save/savegame/",…]`). `config_file.cpp:103-109` writes it,
and it holds `Setup/Resolution` (`app.cpp:582`), `Keybindings` (`input.cpp:342-379`), audio etc.
Scenario: A plays, uploads v1. B signs in, restores v1, the game rewrites `colobot.json` with B's
resolution → flush → upload v2. A closes the tab after a settings write that happened inside the 5 s
debounce (never uploaded). A starts again: `st.base = 1 ≠ 2`, `st.hash ≠ localHash` → **conflict
dialog** about saves that differ only in display settings; whichever side the user picks, the other
device's settings are overwritten and the cycle repeats. Fix: exclude `colobot.json` (and arguably
`mods/`) from `listSave()` for hashing/upload, and never delete it in `replaceSave()`; sync only
`savegame/` (profiles, missions, CBOT programs). Also decide what "offline settings edits" mean before
shipping to real users.

**F4 — Two tabs of the same browser overwrite each other's sync state and can push a 409 → "Use this
device" overwrite.** `web/account.js:211-212,226` loads `{base,hash}` from `localStorage` once per
tab at `setMode` and never re-reads it; each tab also has its own MEMFS copy of `/save` (IDBFS). Tab 1
uploads v6 and writes `base=6`; tab 2 still holds `base=5` in memory, its next flush uploads with
`X-Base-Version: 5` → `409 conflict` → the conflict dialog appears for a "conflict" with the user's own
other tab; "Use this device" uploads tab 2's MEMFS as v7 over tab 1's changes (and tab 2's
`syncfs(false)` already overwrote tab 1's files in IndexedDB — a pre-existing multi-tab hazard of
IDBFS that the sync now propagates to the cloud). Fix: re-read `localStorage` in `upload()` /
`syncUp()` before choosing `base` (or listen to the `storage` event), and take a `navigator.locks`
lock (`colobot-save`) in `onSaveMounted`; when it is held by another tab, show a "game already open in
another tab" notice and disable sync in the second tab.

### Minor

**F5 — Session state survives across users on a shared browser; previous user's saves are uploaded
into the next account.** `account.js:320-325` (sign-out keeps IndexedDB saves, as documented) and
`account.js:412`: a different user who signs in with no cloud saves hits `!latest && entries.length`
→ the previous user's `/save` is silently uploaded to the new account; with cloud saves present it is
offered in the conflict dialog as "this device's version". Fix: on sign-out (and account deletion)
offer "also remove saves from this browser", or at least on a first sign-in with a *different* user id
than the one last synced here (`colobot.sync.<uid>` keys tell you) show the conflict dialog even when
the cloud is empty.

**F6 — Zip reader has no inflated-size cap (self-DoS only).** `web/zip.js:74-87` trusts `usize` from
the central directory and inflates every entry fully before comparing; a 20 MB cloud/imported archive
can expand to gigabytes and kill the tab (it also keeps every `data` buffer alive until
`replaceSave`). Only the user's own data reaches this path (R2 objects are per-user, imports are
local files), so no third-party exposure. Fix: reject when `usize` > e.g. 64 MB or the running total
exceeds a cap, before inflating; also reject `count` with `p` running past `u8.length`. Path handling
is correct: `replaceSave` (`account.js:133-140`) strips `..`, `.`, empty segments and roots everything
at `/save`; zips cannot express symlinks here (external attrs ignored); backslashes become a plain
filename.

**F7 — Malformed cookie → 500 for every API call from that browser.** `worker/index.js:31`
`decodeURIComponent` throws `URIError` on e.g. `colobot_session=%E0%A4%A` (verified: `GET /api/me` →
500, stack in the dev log). Only same-site code can set cookies, so impact is a self-inflicted outage,
but it turns every endpoint into `server_error`. Fix: wrap in `try { … } catch { skip }`.

**F8 — `rate_limits` rows are never pruned, and the email key is retained after account deletion.**
`worker/index.js:69-78,170-182`, `migrations/0001_accounts.sql:38-42`. Every failed login inserts
`login-email:<email>` for arbitrary (non-existent) addresses and `register-ip:<ip>` per IP; nothing
deletes them, so D1 grows without bound and `login-email:<deleted user's email>` outlives the account
(small privacy leak vs. the "removes your account immediately" promise in `privacy.html`). Fix: in
`rateHit`, also `DELETE FROM rate_limits WHERE window_start < ?` every N calls (or a cron trigger), and
delete `login-email:<email>` in `deleteAccount`.

**F9 — No per-user limit on `PUT /api/saves`.** `worker/index.js:300-340`. An authenticated user can
loop 20 MB PUTs (each costs an R2 class-A write + delete and a D1 write); 10 accounts per IP per 15 min
means ~200 MB stored per account and unbounded operations. Fix: reuse `rateCheck`/`rateHit` with a
`put-user` kind (e.g. 60 per 15 min) and/or require `hash !== latest.hash`.

**F10 — Request bodies are buffered before the size check.** `worker/index.js:56-60,314-315`:
`readJson` only trusts `Content-Length` (a chunked body skips the check and `req.json()` buffers it
all); `putSave` reads `arrayBuffer()` then checks `length` (verified: a 25 MB chunked PUT was read
fully, then `413`). With the 128 MB isolate limit this is a cheap way to crash a request, not the
service. Fix: read the stream in chunks and abort past the cap.

**F11 — `onSaveFlushed` is `async` but called synchronously from the C hook.** `web_archiver.c:157`
wraps the call in `try/catch`, but `account.js:426-437` returns a promise, so any rejection
(`listSave` on a half-written FS, `hashEntries`) becomes an unhandled rejection rather than the logged
`onSaveFlushed:` error. Fix: `Module.onSaveFlushed = () => account.hooks.onSaveFlushed().catch(console.warn)`
in `loader.js:102`.

**F12 — Account deletion needs only the session cookie.** `worker/index.js:170-182`. A stolen or
left-behind session (shared computer, 30-day cookie) can irreversibly delete all cloud saves. Fix:
require the current password for password accounts (Google accounts: a fresh `/api/auth/google/start`
round-trip or at least a typed confirmation), or soft-delete with a 7-day grace period.

**F13 — Minor hardening nits.** (a) `worker/index.js:7` cookie could use the `__Host-` prefix (no
subdomain or path override possible). (b) `checkCsrf` relies on `Origin`; add a fallback to
`Sec-Fetch-Site: same-origin` for the rare clients that omit `Origin` on same-origin POST, and make sure
no `Referrer-Policy: no-referrer` is ever added to `index.html`/`_headers` (none today), since that
turns `Origin` into `null` and would break every sign-in. (c) `account.js:77` `errText(code)` with
`code = 'constructor'` from the URL renders `L.errors.constructor` (a function's source) as text —
harmless; use `Object.hasOwn`. (d) `putSave` stores the client-supplied `hash`/`device` verbatim; they
are only ever rendered via `textContent` (`account.js:340`), so no XSS, but `device` could be
whitelisted to `[\w .-]{0,80}`.

### Checked and found correct

- **CSRF**: `Origin === url.origin` on every non-GET, `application/json` on POST, `X-Colobot: 1` on
  PUT/DELETE, plus `SameSite=Lax`. Same-origin `fetch` always sends `Origin` on non-GET. `privacy.html`
  sends `X-Colobot` on DELETE. GET endpoints with cookies (`/api/saves/<n>`) are not readable
  cross-origin (no CORS headers).
- **Sessions**: 32 random bytes b64url, only SHA-256 stored, `HttpOnly; Secure; SameSite=Lax`, 30 days,
  logout deletes the row, length-capped before hashing.
- **OAuth**: state + S256 PKCE in a 10-min HttpOnly cookie scoped to `/api/auth/google`, constant-time
  state compare, `code_verifier` sent on exchange, `iss`/`aud`/`exp`/`sub` checked, `email` only when
  `email_verified`. ID token from the token endpoint over TLS, so skipping signature verification is
  per OIDC Core 3.1.3.7. Login-CSRF via the callback is blocked by the state cookie. Redirect URI is
  built from `url.origin` and validated by Google against the registered list.
- **Account takeover via linking**: there is no linking. Google `sub` → own `users` row with
  `email NULL`; no merge by email; no "add Google" or "add password" endpoint. Correct and documented.
- **Rate limiting**: `rateCheck` before and `rateHit` after the credential check on login; register
  checks IP before body parsing and hits after validation; email counter cleared on success; the
  `429` carries `Retry-After`. No account-existence oracle via the limiter (unknown emails are counted
  too). Lockout of a victim's email for 15 min is possible (10 wrong tries) — accepted trade-off.
- **Authz / SQL**: every `saves` query is bound to `u.id`; all statements parameterised; `getSave`
  version is `(latest|\d+)` only. `UNIQUE COLLATE NOCASE` + lowercasing on email.
- **Upload race**: `(user_id, version)` PK makes the loser `409`; the orphaned R2 object is deleted in
  the catch. FK cascade (verified on local D1) also makes a `putSave` that loses a race with
  `deleteAccount` fail at the insert and delete its object.
- **Delete-account completeness**: R2 keys come from `saves`, batch deletes `saves`, `sessions`,
  `oauth_identities`, `users`, cookie cleared. Only `rate_limits` is missed (F8). Pruning to 10
  versions deletes R2 before D1.
- **XSS**: all UI text goes through `el()`/`textContent`/`append(string)`; nothing uses `innerHTML`.
- **Service worker**: `/api/*` bypassed, cache name bumped to purge responses an older SW might have
  cached.
- **Mount order**: `wfs_js_install_sync()` runs before `wfs_js_mount_save()` (`web_archiver.c:344`),
  so `__wfsFlushNow` exists when `onSaveMounted` restores; `main()` stays suspended until the hook's
  promise settles; a thrown hook only logs and the game continues with local saves.
- **Offline**: `/api/me` failure with a remembered cloud mode → local, not persisted, no prompt;
  `online` and `visibilitychange(hidden)` retrigger sync; a `401` mid-game falls back to local with a
  toast.
- **Debounce**: `schedule()` resets the 5 s timer per flush; `s.again` re-runs when a flush arrives
  during an upload; `hash === s.sync.hash` short-circuits no-op uploads.

## Summary

No exploitable security hole was found: CSRF, sessions, OAuth (state + PKCE), per-user authz,
parameterised SQL, no-link policy and the zip path sanitiser are all sound. The blockers-in-waiting
are operational and correctness issues in the sync model: F1 (plan unknown; PBKDF2 100k likely
exceeds Workers Free CPU), F2 (sync state written before the flush → stale upload over newer cloud),
F3 (device settings file in the sync set → recurring false conflicts), F4 (multi-tab). F1–F4 should be
fixed before the remote migration/deploy; F5–F13 can follow.

VERDICT: OK
