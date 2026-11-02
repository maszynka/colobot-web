# Security and Correctness Review: Accounts & Cloud Saves

**Scope**: `.review/diff3.patch`, `docs/ACCOUNTS.md`, `worker/index.js`, `worker/crypto.js`, `web/account.js`, `web/zip.js`, `web/privacy.html`, `web/loader.js`, `web/sw.js`, `src/colobot-base/src/common/resources/web_archiver.c`, `migrations/0001_accounts.sql`.

---

## Executive Summary & Strengths

The overall architecture is well-structured:
- **Authentication**: Passwords use PBKDF2-SHA256 with per-user 16-byte random salts. Sessions use 32 random bytes stored in an `HttpOnly; Secure; SameSite=Lax` cookie and verified via SHA-256 in D1. Dummy PBKDF2 derivations run for nonexistent accounts during login to mitigate timing attacks.
- **OAuth 2.0**: The Google authorization-code flow incorporates PKCE (`S256`) and an HMAC/timing-safe verified `state` cookie. Google identities are strictly separated from password accounts (no automatic account-linking by email), eliminating pre-registration takeover attacks.
- **Authorization & Isolation**: Every save endpoint enforces session ownership (`user_id = u.id`) in D1 queries. R2 keys are isolated under `u/<user_id>/<version>-<rand>.zip` and cannot be forged or accessed by other users.
- **D1 SQL Injection**: All D1 database queries use parameterized SQL (`prepare(...).bind(...)`). No dynamic SQL string interpolation exists.
- **CSRF & XSS**: State-changing API routes check same-origin `Origin`, enforce JSON content-types on POST, and require custom headers (`X-Colobot: 1`) on PUT and DELETE. UI rendering in [account.js](../../web/account.js) and [privacy.html](../../web/privacy.html) relies entirely on `textContent` and DOM nodes without `innerHTML`.
- **Account Deletion**: [deleteAccount](../../worker/index.js) retrieves all user R2 keys and deletes them via `env.SAVES.delete(keys)` before atomically deleting database rows in `saves`, `sessions`, `oauth_identities`, and `users`.

However, several critical and major issues were identified in rate limiting (unauthenticated account lockout DoS), offline synchronization state handling, zip decompression limits, and cross-user data isolation on shared devices.

---

## Findings

### Finding 1: Unauthenticated Account Lockout Denial of Service via `login-email` Rate Limiting
- **Severity**: Blocker
- **File:Line**: [worker/index.js:1394](../../worker/index.js), [worker/index.js:1310-1324](../../worker/index.js)
- **Scenario**:
  In [`login()`](../../worker/index.js), `rateCheck(env, 'login-email', email)` is executed **before** validating the user's password. The threshold `LIMITS['login-email']` is set to 10 attempts per 15 minutes (`LOGIN_WINDOW`).
  An unauthenticated attacker can send 10 failed login requests with arbitrary passwords targeting any victim's email address (`victim@example.com`).
  Once `rate_limits` reaches 10, any subsequent login attempt for `victim@example.com` immediately throws `429 too_many_attempts` in `rateCheck` before password verification is reached.
  The legitimate user who knows their correct password is completely locked out of their account for 15 minutes. An attacker can trivially automate 1 request every 15 minutes to lock legitimate users out indefinitely.
- **Fix**:
  Do not enforce a hard lockout on an email address alone across all IPs before checking credentials. Either:
  1. Rate-limit failed logins using a composite key `(clientIp, email)` to prevent single-source credential stuffing; or
  2. If global email rate limiting is desired, only invoke `rateHit` for the email after password verification fails, and require CAPTCHA (such as Cloudflare Turnstile) upon exceeding the threshold rather than rejecting all authentication attempts.

---

### Finding 2: Offline Startup Permanently Switches Cloud Mode to Local, Disabling Sync and Causing Data Loss
- **Severity**: Blocker
- **File:Line**: [web/account.js:883](../../web/account.js), [web/account.js:837](../../web/account.js), [web/account.js:864](../../web/account.js)
- **Scenario**:
  When a player who chose `cloud` mode opens the game without an internet connection (or if `/api/me` is unreachable), line 883 executes:
  ```javascript
  if (!me && stored === 'cloud') { s.offline = true; return setMode('local', null, false); }
  ```
  This mutates `s.mode` to `'local'`. Consequently:
  1. The `online` window event listener (`addEventListener('online', () => s.mode === 'cloud' && schedule(1000))` at line 837) checks `s.mode === 'cloud'`. Because `s.mode` is `'local'`, reconnection never resumes sync.
  2. [`onSaveFlushed()`](../../web/account.js) checks `if (s.mode === 'cloud') { schedule(5000); return; }`. Because `s.mode` is `'local'`, in-game progress made during the offline session is never scheduled for cloud sync.
  3. [`renderBadge()`](../../web/account.js) renders `'⌂ Local'` instead of `'☁ ... · offline'`, giving the user incorrect status.
  If the player plays offline and reconnects, their progress is not synced. If the browser cache/site data is cleared or the tab is closed without a manual reload, the player's new saves are permanently lost.
- **Fix**:
  Preserve `s.mode = 'cloud'` when offline:
  ```javascript
  if (!me && stored === 'cloud') {
    s.offline = true;
    s.mode = 'cloud';
    s.sync = loadSync();
    renderBadge();
    decide();
    return;
  }
  ```
  When the `online` event fires, re-query `/api/me`, clear `s.offline`, update the badge, and trigger `syncUp(false)`.

---

### Finding 3: Zip Bomb / Unbounded Decompression Memory Exhaustion (Client Tab DoS)
- **Severity**: Major
- **File:Line**: [web/zip.js:1088-1090](../../web/zip.js), [web/zip.js:1150-1165](../../web/zip.js), [worker/index.js:1547-1563](../../worker/index.js)
- **Scenario**:
  In [`readZip()`](../../web/zip.js), each compressed entry is decompressed with `new DecompressionStream('deflate-raw')` and consumed via `await new Response(...).arrayBuffer()`.
  Neither the header `usize` nor the streaming output byte count is checked against a maximum threshold during inflation. There is also no cumulative decompressed size check across all archive members.
  If a user imports a crafted zip file via "Import saves" (`#acct-menu -> fileIn`), or if a cloud save contains a zip bomb (e.g. 100 KB compressed expanding to multiple gigabytes), `readZip` will exhaust browser memory and freeze or crash the tab (Out of Memory crash).
  On the Worker, [`putSave()`](../../worker/index.js) checks that the compressed body is $\le 20$ MB, but does not inspect or constrain entry count or uncompressed dimensions.
- **Fix**:
  Enforce strict decompression limits in [web/zip.js](../../web/zip.js):
  1. Validate `usize <= 25 * 1024 * 1024` for each entry before inflating.
  2. Maintain a cumulative decompressed byte counter across all entries and abort if it exceeds a cap (e.g. 50 MB total).
  3. Impose a maximum entry count limit (e.g. 500 entries).

---

### Finding 4: Cross-User Save Data Leakage on Shared Devices via Unscoped `/save`
- **Severity**: Major
- **File:Line**: [web/account.js:757-762](../../web/account.js), [web/account.js:849](../../web/account.js)
- **Scenario**:
  When User A logs out ([`signOut()`](../../web/account.js)), their session is deleted on the server, but User A's `/save` directory remains in the browser's IndexedDB.
  If User B opens the game in the same browser and registers a new account:
  Line 849 of [`onSaveMounted()`](../../web/account.js) runs:
  ```javascript
  if (!latest) { if (entries.length) schedule(500); return; }
  ```
  Because User B is new, `latest` is null, but `entries` contains User A's existing local saves.
  The client automatically uploads User A's full save archive (player profiles, missions, custom CBOT programs) to User B's new cloud account without warning or confirmation. User B now permanently owns and can access User A's saves from any device.
- **Fix**:
  When registering or signing into an account with no cloud saves (`!latest`) where local saves already exist (`entries.length > 0`), check whether the local sync state matches the authenticated user ID. If there is no previous association with this user, prompt the user with a confirmation dialog (e.g., *"Upload existing local saves to this account?"*) or isolate local saves per user.

---

### Finding 5: `importZip` Reloads Immediately, Masking Sync Errors and Racing Conflict Prompts
- **Severity**: Major
- **File:Line**: [web/account.js:753-754](../../web/account.js)
- **Scenario**:
  In [`importZip(file)`](../../web/account.js):
  ```javascript
  if (s.mode === 'cloud') await syncUp(true);
  location.reload();
  ```
  1. If `syncUp(true)` fails with a network error, `syncUp` catches the error internally, displays `toast(L.syncFail)`, and returns. `importZip` immediately invokes `location.reload()`, destroying the page before the toast can render and leaving the user believing the imported save was successfully synced to the cloud.
  2. If `syncUp(true)` encounters a 409 conflict, `syncUp` awaits the interactive `conflict` modal. If resolved, both `restoreFromCloud` and `importZip` issue competing `location.reload()` calls.
- **Fix**:
  Ensure `syncUp` propagates or returns sync outcome. Only reload if sync succeeded (or in local mode). If sync failed, do not reload; leave the toast visible so the player knows the cloud upload did not succeed.

---

### Finding 6: Chunked Transfer-Encoding Bypasses Size Limits in `readJson` and `putSave` (Worker DoS)
- **Severity**: Minor
- **File:Line**: [worker/index.js:1301-1305](../../worker/index.js), [worker/index.js:1547-1560](../../worker/index.js)
- **Scenario**:
  In [`readJson`](../../worker/index.js):
  ```javascript
  const len = +(req.headers.get('Content-Length') || 0);
  if (len > 16384) throw new HttpError(413, 'too_large');
  ```
  If a request uses `Transfer-Encoding: chunked`, `Content-Length` is absent, setting `len` to 0. `req.json()` then reads an unbounded stream into memory.
  In [`putSave`](../../worker/index.js), a chunked request similarly bypasses the initial `Content-Length` check and executes `await req.arrayBuffer()`, buffering up to the Worker's 128 MB RAM limit before `body.length > MAX_SAVE_BYTES` is evaluated, risking Worker OOM crashes (Error 1101).
- **Fix**:
  For JSON endpoints, require `Content-Length` or limit stream consumption:
  ```javascript
  if (!req.headers.has('Content-Length') || len > 16384) throw new HttpError(413, 'too_large');
  ```
  In `putSave`, stream chunks with a byte-counting transform stream to abort early if bytes exceed `MAX_SAVE_BYTES`.

---

### Finding 7: 100k PBKDF2 Iterations Incompatible with Workers Free Tier (10 ms CPU Limit)
- **Severity**: Minor / Operational
- **File:Line**: [worker/crypto.js:1176](../../worker/crypto.js), [worker/crypto.js:1206-1210](../../worker/crypto.js), [docs/ACCOUNTS.md:113](../../docs/ACCOUNTS.md)
- **Scenario**:
  `PBKDF2_ITER` is hardcoded to 100,000 iterations. In V8 WebCrypto on Cloudflare Workers, 100k iterations of PBKDF2-SHA256 requires 30–70 ms of CPU time.
  The Cloudflare Workers Free tier has a 10 ms CPU execution limit. On Free plans, every login and registration attempt will fail with Error 1102 (CPU Time Limit Exceeded).
  While documented as an "occasional" occurrence in `docs/ACCOUNTS.md`, 100k iterations deterministically exceeds 10 ms on standard hardware.
- **Fix**:
  Clearly document in deployment instructions that a Cloudflare Workers Paid plan (with standard 50 ms / 30 s CPU limits) is strictly required, or allow `PBKDF2_ITER` to be configured via environment variables for dev/free testing environments.

---

### Finding 8: Non-Existent `crypto.subtle.timingSafeEqual` Reference
- **Severity**: Minor
- **File:Line**: [worker/crypto.js:1217](../../worker/crypto.js)
- **Scenario**:
  [`timingSafeEqual`](../../worker/crypto.js) contains:
  ```javascript
  if (crypto.subtle.timingSafeEqual) return crypto.subtle.timingSafeEqual(a, b);
  ```
  The W3C Web Cryptography API (`SubtleCrypto`) does not define a `timingSafeEqual` method. This property is always `undefined`, falling through to the pure-JS XOR loop.
- **Fix**:
  Remove line 1217 or import `timingSafeEqual` from `node:crypto` (available under `nodejs_compat`).

---

### Finding 9: Unbounded `rate_limits` Table Growth in D1
- **Severity**: Minor
- **File:Line**: [worker/index.js:1314-1323](../../worker/index.js)
- **Scenario**:
  `sessions` records are pruned opportunistically during session creation (`DELETE FROM sessions WHERE expires_at < ?`).
  However, `rate_limits` entries (such as `register-ip:*` and failed `login-ip:*`) are never deleted once their window expires. Every unique client IP that ever makes an invalid attempt remains in D1 indefinitely, accumulating stale rows.
- **Fix**:
  Add opportunistic cleanup in `rateHit` or a periodic query:
  ```sql
  DELETE FROM rate_limits WHERE ? - window_start >= ?;
  ```

---

VERDICT: BLOCKING
