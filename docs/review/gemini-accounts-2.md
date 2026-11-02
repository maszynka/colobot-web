# Round-2 Re-Review: Accounts & Cloud Saves

**Scope**: `.review/diff4.patch`, `docs/ACCOUNTS.md`, `docs/review/response-accounts-1.md`, `worker/index.js`, `worker/crypto.js`, `web/account.js`, `web/zip.js`, `web/privacy.html`, `web/loader.js`, `migrations/0002_rate_limits_prune.sql`, `tests/e2e/accounts-api.mjs`, `tests/e2e/accounts.mjs`, `tests/unit/zip-limits.mjs`.

---

## 1. Executive Summary

All critical and major security and data-loss vulnerabilities identified in Round 1 ([gemini-accounts-1.md](file://<repo>/docs/review/gemini-accounts-1.md) and [fable-accounts-1.md](file://<repo>/docs/review/fable-accounts-1.md)) have been resolved. The fix implementations in `.review/diff4.patch` are correct, robust, and supported by thorough automated test coverage (`tests/e2e/accounts-api.mjs`, `tests/e2e/accounts.mjs`, `tests/unit/zip-limits.mjs`).

The author's rejection of Gemini Finding 8 (`crypto.subtle.timingSafeEqual`) was evaluated and found **technically justified**; a defensive runtime check and portable constant-time fallback were implemented.

No new security holes, data loss vectors, or regressions were introduced in this round.

---

## 2. Verification of Round 1 Fixes

### Gemini Finding 1: Unauthenticated Account Lockout DoS via `login-email`
- **Status**: **Verified Fixed**
- **Analysis**: Global per-email rate limiting was removed. Instead, rate limiting uses:
  1. A per-IP counter `LIMITS['login-ip'] = 30` per 15-minute window ([worker/index.js:20](file://<repo>/worker/index.js#L20)).
  2. Per-`(IP, email)` exponential backoff ([worker/index.js:126-145](file://<repo>/worker/index.js#L126-L145)) after 5 free failures (`2^(n - 5 + 1)` seconds, max 15 minutes).
  An attacker attempting to brute-force a victim's email from one IP only throttles their own IP/email pair; the victim on their legitimate IP remains unblocked. Migration `0002_rate_limits_prune.sql` cleanly drops legacy `login-email:*` keys.

### Gemini Finding 2: Offline Startup Permanently Switching Cloud Mode to Local
- **Status**: **Verified Fixed**
- **Analysis**: In [web/account.js:655-663](file://<repo>/web/account.js#L655-L663), when `/api/me` is unreachable and `stored === 'cloud'`, `colobot.user` is read to preserve `s.mode = 'cloud'`, `s.offline = true`, and `setMode('cloud', lastUser, false)` (persisting nothing so the cloud preference is not corrupted). `markOffline()` sets up exponential reconnect retries, and `window.addEventListener('online')` triggers `reconnect()`. Pending offline saves flush to IndexedDB and are synced upon reconnection.

### Gemini Finding 3: Zip Bomb / Memory Exhaustion Tab DoS
- **Status**: **Verified Fixed**
- **Analysis**: Strict caps (`ZIP_LIMITS = { entries: 5000, entryBytes: 32MB, totalBytes: 64MB }`) are enforced in [web/zip.js:35](file://<repo>/web/zip.js#L35). In [web/zip.js:18-32](file://<repo>/web/zip.js#L18-L32), `inflate(d, cap)` consumes `DecompressionStream('deflate-raw')` via chunks, aborts the reader, and throws immediately if inflated bytes exceed `cap` (`usize`). Cumulative inflated byte count across all entries is strictly bounds-checked against `totalBytes`.

### Gemini Finding 4: Cross-User Save Data Leakage on Shared Devices
- **Status**: **Verified Fixed**
- **Analysis**: `colobot.owner` tracks the user ID owning local saves ([web/account.js:104](file://<repo>/web/account.js#L104)). When a different user signs in on a device holding saves from another account, [confirmOwner](file://<repo>/web/account.js#L455-L479) presents a modal allowing the player to either explicitly adopt the saves or "Start fresh" (which wipes local saves via `clearLocal()`). On sign out ([web/account.js:385-405](file://<repo>/web/account.js#L385-L405)), the user is given a choice to keep or remove saves from the browser.

### Gemini Finding 5: `importZip` Reloading Over Failed or Conflicting Sync
- **Status**: **Verified Fixed**
- **Analysis**: In [web/account.js:363-375](file://<repo>/web/account.js#L363-L375), `importZip` awaits any running background upload (`while (s.uploading) await s.uploading`). It evaluates the returned sync outcome: if conflict resolution triggered a reload (`reloading`), it returns; if sync failed, it displays `toast(L.importPending)` with a manual Reload action rather than abruptly reloading the page and discarding unsaved context.

### Gemini Finding 6: Chunked Transfer-Encoding Bypassing Size Limits
- **Status**: **Verified Fixed**
- **Analysis**: In [worker/index.js:71-91](file://<repo>/worker/index.js#L71-L91), `readBody(req, max)` counts incoming stream chunks and immediately calls `reader.cancel()` and throws `413 too_large` once total bytes exceed `max`. Both `readJson` (capped at 16 KB) and `putSave` (capped at 20 MB) consume request bodies exclusively through `readBody`.

### Gemini Finding 7: 100k PBKDF2 Iterations vs. Workers Free CPU Limit
- **Status**: **Verified Fixed**
- **Analysis**: Default PBKDF2 iterations were reduced to 20,000 ([worker/crypto.js:8](file://<repo>/worker/crypto.js#L8)), measured at ~2 ms per derivation. The iteration count is configurable via `env.PBKDF2_ITER` and persisted per user row (`password_iter`). Logins on accounts with older or lower iteration counts automatically re-hash and update D1 with the current target cost ([worker/index.js:232-237](file://<repo>/worker/index.js#L232-L237)).

### Gemini Finding 9: Unbounded `rate_limits` Table Growth
- **Status**: **Verified Fixed**
- **Analysis**: [worker/index.js:132-135](file://<repo>/worker/index.js#L132-L135) implements opportunistic pruning (`ratePrune`) executed randomly on ~1 in 16 writes, purging rows older than `2 * WINDOW` (30 minutes) using index `rate_limits_start`. [deleteAccount](file://<repo>/worker/index.js#L278-L280) also purges all `login-ipemail:*|<email>` and `put-user:<id>` keys.

---

## 3. Assessment of Author Rejection

### Gemini Finding 8: `crypto.subtle.timingSafeEqual`
- **Reviewer Round-1 Claim**: `crypto.subtle.timingSafeEqual` does not exist in the W3C SubtleCrypto standard.
- **Author Response**: Rejected. `crypto.subtle.timingSafeEqual` is a Cloudflare Workers (`workerd`) proprietary extension.
- **Re-review Assessment**: **Rejection is valid**. Cloudflare's runtime implements `crypto.subtle.timingSafeEqual` natively. Furthermore, the author hardened the implementation in [worker/crypto.js:52-60](file://<repo>/worker/crypto.js#L52-L60) by guarding with `typeof crypto.subtle.timingSafeEqual === 'function'` and retaining a constant-time XOR fallback loop for standard WebCrypto runtimes (such as Node.js test environments).

---

## 4. Verification of Other Reviewer Findings (Fable F1–F13)

- **F2 (Restore Order Race)**: [web/account.js:487-490](file://<repo>/web/account.js#L487-L490) flushes to IndexedDB via `await flush()` *before* updating `storeSync`. If the flush fails or the tab crashes mid-restore, stale sync state is never committed.
- **F3 (`colobot.json` Device Settings Conflict Loop)**: [web/account.js:128-143, 153-162](file://<repo>/web/account.js#L128-L143) excludes `/save/colobot.json` from `listSave` (cloud hashing and archive creation) and preserves local settings in `replaceSave`. Manual "Download saves" retains the settings file (`all: true`).
- **F4 (Multi-Tab Overwrites)**: [web/account.js:609-619](file://<repo>/web/account.js#L609-L619) acquires the Web Lock `colobot-save` (`ifAvailable: true`). Secondary tabs are marked `secondary = true`, display a warning toast, and refrain from background sync or uploads.
- **F7 (Malformed Cookie 500)**: [worker/index.js:42](file://<repo>/worker/index.js#L42) wraps `decodeURIComponent` in a `try/catch` and skips malformed cookie entries.
- **F9 (Per-User Save Rate Limit)**: [worker/index.js:405-406](file://<repo>/worker/index.js#L405-L406) limits save uploads per user account (`LIMITS['put-user'] = 60` per 15 minutes).
- **F11 (Unhandled `onSaveFlushed` Rejections)**: [web/loader.js:1322](file://<repo>/web/loader.js#L1322) adds `.catch((e) => console.warn('onSaveFlushed:', e))` to the C-to-JS bridge call.
- **F12 (Account Deletion Re-Authentication)**: [worker/index.js:252-268](file://<repo>/worker/index.js#L252-L268) requires `{ confirm: "delete" }`, verifies the user's password for password accounts (rate-limited via `login-ip` and `loginBackoff`), and requires a session younger than 10 minutes (`REAUTH_AGE`) for Google OAuth accounts.
- **F13 (Hardening Nits)**: Cookie prefixed with `__Host-` ([worker/index.js:8](file://<repo>/worker/index.js#L8)), CSRF checks accept `Sec-Fetch-Site: same-origin` fallback ([worker/index.js:60](file://<repo>/worker/index.js#L60)), error lookup uses `Object.hasOwn` ([web/account.js:99](file://<repo>/web/account.js#L99)), and device label is sanitized with `[\w .()-]` ([worker/index.js:411](file://<repo>/worker/index.js#L411)).

---

## 5. Non-Blocking Observations & Recommendations

These items do not represent security vulnerabilities or data loss hazards but are noted for operational awareness:

### Observation 1: Distributed Credential Stuffing Across Botnet IPs
- **Severity**: Low / Informational
- **File:Line**: [worker/index.js:126-145](file://<repo>/worker/index.js#L126-L145), [docs/ACCOUNTS.md:63](file://<repo>/docs/ACCOUNTS.md#L63)
- **Detail**: Because email-only global rate limiting was removed to prevent lockout DoS, an adversary using a large distributed botnet (each request from a distinct IP) could test passwords against a single email without triggering IP rate limits. This is an accepted and documented design trade-off.
- **Recommendation**: If distributed credential stuffing occurs post-launch, embed Cloudflare Turnstile on `/api/auth/login`.

### Observation 2: Production Worker CPU Limit Tail Monitoring
- **Severity**: Low / Operational
- **File:Line**: [worker/crypto.js:8](file://<repo>/worker/crypto.js#L8), [docs/ACCOUNTS.md:127](file://<repo>/docs/ACCOUNTS.md#L127)
- **Detail**: While 20,000 PBKDF2 iterations tested at ~2 ms in local workerd (well below 10 ms Free CPU limits), production CPU allocations can fluctuate under load.
- **Recommendation**: Run `wrangler tail` on initial production deployment during registration and login events to confirm error 1102 does not trigger.

---

VERDICT: OK
