# Review round 2: accounts + cloud saves (fable-accounts-2)

Scope: the fix series `git diff 947997b~1 HEAD -- . ':!package-lock.json'` (`.review/diff4.patch`, commits
`947997b` … `1272c39`), read against `worker/index.js`, `worker/crypto.js`, `migrations/0001`+`0002`,
`web/account.js`, `web/zip.js`, `web/loader.js`, `web/privacy.html`,
`src/colobot-base/src/common/resources/web_archiver.c`, `docs/ACCOUNTS.md`, and the author response
`docs/review/response-accounts-1.md`. Read-only except this file.

What was run (all green):

- `node tests/unit/zip-limits.mjs`: ok (total cap, lying `usize`, truncation, roundtrip under raised limits).
- `node tests/e2e/accounts-api.mjs --port 8795`: **15/15 PASS** (backoff `[401×5, 429…]`, `Retry-After: 2`,
  victim on another IP `200`, malformed cookie `200/401`, chunked 2 MB JSON and 25 MB PUT → `413`,
  60th upload → `429`, DELETE 400/403/200, no `api error` in the dev log).
- `node tests/e2e/accounts.mjs --port 8796`: **18/18 PASS** (cloud archive without `colobot.json`,
  settings-only change → `unchanged`, second tab `secondary`, offline start stays cloud, pending change
  uploaded as v4 after `online`, "Start fresh" uploads nothing, local mode never touches `/api/saves`).
- Own probe (scratchpad script against `wrangler dev`): register at the default → `password_iter 20000`;
  restart with `--var PBKDF2_ITER:30000`; a *wrong* login leaves the row unchanged; a correct login rewrites
  hash, salt and `password_iter = 30000`; the next login succeeds. Rehash-on-login works as described.

## Round-1 fixes, verified against the code

| # | finding | status | evidence |
| --- | --- | --- | --- |
| F1 / G7 | PBKDF2 vs Free CPU | **fixed** | `crypto.js:8-13` default 20k, `iterations(env)` clamps 10k–100k; `index.js:224-237` verifies at the stored count, re-hashes when `password_iter !== iter` (probe above). Measurement table in `docs/ACCOUNTS.md`. |
| F2 | sync state before flush | **fixed** | `account.js:484-491`: `replaceSave → flush() → storeSync → setOwner`. `syncUp` also flushes before zipping (`:570-574`); its own flush is ignored by the hook via `ownFlush` (the hook fires inside the `.then` of the running `syncfs`, `web_archiver.c:152-158`, so the counter is still > 0 at that moment — checked). |
| F3 | `colobot.json` synced | **fixed** | `listSave` skips it unless `all` (`:135`), `replaceSave` never deletes it (`:157`), restore filters it from the archive (`:486`). e2e confirms the archive and the `unchanged` result. |
| F4 | two tabs | **fixed** | Web Lock `colobot-save`, `ifAvailable`, held for the tab's life (`:609-618`); `reconcile`/`syncUp` bail on `s.secondary`; `s.sync = loadSync()` re-read before every upload (`:575`). |
| F5 / G4 | shared browser | **fixed** | `colobot.owner` + `confirmOwner` in every upload path (`reconcile :524,527`, `syncUp :577`); sign-out dialog keep/remove with the unsynced warning (`:385-405`). "Start fresh" clears `/save` (keeps settings) and sets the owner. |
| F6 / G3 | zip bomb | **fixed** | `zip.js:35` caps (5000 / 32 MB / 64 MB), `inflate(raw, usize)` counts and cancels the stream (`:17-32`), central-directory and local-header offsets bounds-checked (`:95,105,107`), `data.length !== usize` + CRC check (`:113`). |
| F7 | malformed cookie 500 | **fixed** | `parseCookies` try/catch per part (`index.js:42`). Test: `200 user:null`, `401`. |
| F8 / G9 | `rate_limits` growth + email retention | **fixed** | `ratePrune` ~1/16 writes, `window_start < t − 2·WINDOW` (`:117-121`); index in `0002`; `deleteAccount` removes `login-ipemail:*|<email>` (via `substr(key, -len)`, correct SQLite semantics) and `put-user:<id>` (`:277-280`); `0002` drops the old `login-email:*` rows. |
| F9 | per-user upload limit | **fixed** | `put-user` 60/15 min (`:405-406`). |
| F10 / G6 | buffered bodies | **fixed** | `readBody` counts the stream, cancels past `max` (`:71-91`); `readJson` 16 KB, `putSave` 20 MB. |
| F11 | unhandled hook rejection | **fixed** | `loader.js:103` `.catch`; `syncUp` never rejects (`:596`). |
| F12 | delete with cookie only | **fixed** | `{confirm:"delete"}` always; password accounts verify the password under the login limiter/backoff; Google needs `sessionAge ≤ 600 s` else `403 reauth_required` (`:251-268`). `privacy.html` sends the password too. |
| F13a | `__Host-` prefix | **fixed** | `index.js:8`; `cookie()` emits `Path=/; Secure`, no `Domain`. |
| F13b | `Origin` fallback | **fixed** | `Sec-Fetch-Site: same-origin` accepted only when `Origin` is absent (`:59-61`). |
| F13c | `errText('constructor')` | **fixed** | `Object.hasOwn` (`account.js:99`). |
| F13d | `device`/`hash` verbatim | **partly, acceptable** | `X-Device` whitelisted (`:411`); `X-Save-Hash` only length-capped. It is compared and rendered via `textContent`, so no issue. |
| G1 | lockout DoS via email key | **fixed** | No key contains the email alone (`:18-20,126`). `login-ip` 30/15 min (failures), `(ip,email)` backoff 2^n s after 5 free failures, reset after 15 quiet minutes; counter cleared on success. No account-existence oracle: unknown emails are counted and backed off the same way. |
| G2 | offline start drops cloud mode | **fixed** | `colobot.user` remembered; `setMode('cloud', lastUser, false)` + `markOffline()` (`:655-662`); `onSaveMounted` sets `pendingReconcile`, `reconnect()` reconciles (`live = true`) or uploads; `online` event re-queries `/api/me`. The start/mount/reconnect interleavings (`reconnect` finishing before or after `onSaveMounted`) all end in exactly one reconcile — traced, no gap found. |
| G5 | `importZip` reloads over a failed sync | **fixed** | `syncUp` returns an outcome; import reloads only on `synced`/`unchanged`, leaves the reload to the conflict path on `reloading`, otherwise toast + "Reload" (`:358-376`). |
| G8 | `timingSafeEqual` "does not exist" | **rejection correct** | `crypto.subtle.timingSafeEqual` is a documented Workers extension; the JS fallback covers Node. Length check first is fine (length is not secret here). |

Rejections / partials in the response are all justified. The residual risk the author lists (credential
stuffing spread over many IPs) is a real but accepted limitation; Turnstile is the right next step if it
appears.

## New observations (none blocking)

**R1 — Google "re-auth" for deletion is `prompt=select_account`, not a fresh authentication.**
`index.js:300`. The 10-minute rule defends against a *stolen cookie* (the attacker's browser has no Google
session for the victim) — good. On a *shared desktop* where the victim is still signed in to Google, the
"Sign in with Google" button in the delete dialog is one click: Google will not ask for a password. If the
shared-computer case matters, start the reauth round-trip with `prompt=login` (or `max_age=0`), e.g.
`/api/auth/google/start?reauth=1`. Low severity; the attacker in that scenario already owns the Google
session.

**R2 — `login-ip` is a hard block that also stops correct passwords.** `index.js:219`. 30 failed logins from
one egress IP (a school/office NAT, a mobile carrier) lock every user behind it for up to 15 min, including
those typing the right password. This is the documented trade-off (and the limit must stay pre-verification
to mean anything), so it stays; just be aware the lockout DoS moved from "per email" to "per shared IP".
Turnstile would remove it.

**R3 — A `429`/5xx on `PUT` is not retried until the next flush.** `account.js:591-594`: only `network`
errors call `markOffline()`; `too_many_attempts` and `http_5xx` just toast. Not a data-loss issue: the next
flush or the next start ("only this device changed", `:526`) uploads the pending state, and the `put-user`
budget (60/15 min with a 5 s debounce) is hard to exhaust in normal play. Honouring `Retry-After` with a
`schedule(retryAfter*1000)` would be a cheap improvement. Also `rateHit('put-user')` runs before the
`409`/`413` checks, so conflicts and oversize bodies consume the budget too (`:405-406`).

**R4 — `replaceSave` is not atomic (pre-existing, not a regression).** `account.js:153-171`: `rm('/save')`
then writes; if `FS.writeFile` throws mid-way (a name MEMFS rejects), `/save` is half-replaced in memory and
the game's own later flush persists that. `restoreFromCloud` correctly leaves the sync state untouched, so
the next start shows the conflict dialog rather than uploading; `importZip` surfaces a toast. Only reachable
with an archive the user crafted themselves. A "write to `/save.tmp`, then swap" or at least "validate all
paths before `rm`" would close it.

**R5 — Secondary-tab edge cases.** (a) Import in a secondary tab: `syncUp` → `skipped`, the toast says
"upload failed — it will retry", but a secondary tab never retries; the reload button then makes the
primary tab's next flush win. (b) "Sign out and remove saves" from a secondary tab clears IndexedDB under
the primary tab (its next flush rewrites them). Both need the user to ignore the "open in another tab"
notice. Hiding import/sign-out-remove when `s.secondary` would do.

**R6 — Transitional timing oracle after a change of `PBKDF2_ITER`.** `index.js:224-225`: a real user is
verified at the *stored* count, an unknown email at the *env* count. Between raising the var and the user's
next login, existing vs non-existing emails differ by (new − old) iterations (~1 ms per 10k). Negligible,
disappears per user on first login.

**R7 — Nits.** `migrations/0001_accounts.sql:39` comment still lists `login-email:a@b`; `deleteAccount`
uses a `'\u0000'` sentinel to make the email statement a no-op for Google users (`:279`) — works, a
conditional batch would read better; `privacy.html` clears `colobot.mode`/`colobot.user` but not
`colobot.sync.<id>` (harmless, the id is gone); the account-deletion client path leaves
`colobot.owner = <deleted id>`, so the same person re-registering is asked "saves from another account"
(safe, slightly odd).

## Checked and still correct after the fixes

CSRF (`Origin`/`Sec-Fetch-Site`, JSON on POST, `X-Colobot` on PUT/DELETE, `SameSite=Lax`); session
token hashing and `__Host-` attributes; OAuth state + PKCE, `iss/aud/exp/sub`, verified-email only,
no auto-linking; every `saves` query bound to `u.id`; parameterised SQL throughout, including the new
`rate_limits` statements; upload race on `(user_id, version)` with R2 cleanup; delete-account completeness
(now including `rate_limits`); zip path sanitiser in `replaceSave`; no `innerHTML`; SW bypass of `/api/*`;
mount order (`wfs_js_install_sync` before `wfs_js_mount_save`, `main()` suspended until the hook settles).
Secret scan of the diff: no secrets; `.dev.vars` still gitignored. Nothing deployed, so the `__Host-`
rename and migration 0002 have no live-session or data impact.

## Summary

All 13 Fable and 9 Gemini findings are either fixed as described and verified against the code and the
test runs (18/18, 15/15, zip unit, plus an own rehash probe), or rejected with a correct justification
(G8). No regression found in the touched paths: offline cloud mode, restore order, settings exclusion,
multi-tab lock, owner check, zip caps, streamed body limits, `__Host-` cookie, delete re-auth and
migration 0002 behave as documented. The new observations (R1–R7) are hardening and UX items, none of them
a security hole or a data-loss path. Before the first production deploy: apply `0002` remotely, then
`wrangler tail` the first sign-ins for `Exceeded CPU Limit`, as the docs already say.

VERDICT: OK
