# Response to the accounts reviews (gemini-accounts-1, fable-accounts-1)

Every finding was checked against the code before fixing. Commits are on `main` (not pushed, not deployed).

| commit | content |
| --- | --- |
| `947997b` | PBKDF2 20k default, `PBKDF2_ITER` var, rehash on login, timingSafeEqual comment |
| `f4519e8` | login limits per IP + per (IP, email) backoff, `rate_limits` pruning, migration 0002 |
| `b810539` | stream-limited bodies, per-user upload limit, cookie parsing, delete re-auth, `__Host-` cookie, `Sec-Fetch-Site` fallback, `tests/e2e/accounts-api.mjs` |
| `0865a2e` | zip reader caps + `tests/unit/zip-limits.mjs` |
| `0a1a36a` | `account.js`: offline cloud mode, restore order, `colobot.json` out of sync, Web Lock, owner check, sign-out choice, import outcome, delete dialog |
| `b0f8d69` | `tests/e2e/accounts.mjs`: settings exclusion, second tab, offline start + reconnect, shared browser |
| `29781c2` + this commit | `docs/ACCOUNTS.md`, this file |

## PBKDF2 measurement (Gemini 7, Fable F1)

workerd via `wrangler dev` 4.156 on an Apple M4 Pro, a probe Worker running `importKey` + `deriveBits` (SHA-256, 256 bits) 20 times per count. `Date.now()` and `performance.now()` advance during CPU work in local workerd, and they matched the external wall clock:

| iterations | ms per derivation |
| --- | --- |
| 10 000 | 0.95 |
| 20 000 | 1.95 |
| 30 000 | 2.95 |
| 50 000 | 4.9 |
| 100 000 | 9.8 |

So 100k would take the whole Free-plan budget (10 ms) on a fast laptop core and go over it on a server core. **Chosen: 20 000 iterations** (about 2 ms here, about 4–6 ms if production is 2–3× slower). The count stays per hash (`password_iter`). The var `PBKDF2_ITER` (10k–100k) raises it, and the next successful login re-hashes. One caveat: that one login does two derivations.

## Gemini (VERDICT BLOCKING)

| # | finding | verdict | where / evidence |
| --- | --- | --- | --- |
| 1 | lockout DoS through the per-email limit | **fixed** `f4519e8` | No counter is keyed by the email alone. Failed logins: 30 per IP per 15 min, and per (IP, email) exponential backoff after 5 failures (2, 4, 8 … s, capped at 15 min). Test `accounts-api.mjs`: 12 wrong passwords from 203.0.113.9 → `[401×5, 429…]` with `Retry-After: 2`; the victim from 198.51.100.7 → `200`. |
| 2 | an offline start drops cloud mode for good | **fixed** `0a1a36a` | `colobot.user` remembers the user. An offline start stays in cloud mode (badge "offline"), retries with backoff and on `online`, then reconciles or uploads the pending change. e2e: API blocked → mode `cloud`, `colobot.mode` still `cloud`, `syncNow()` = `failed`; unblocked → v4 contains `offline-test.txt`. |
| 3 | zip bomb | **fixed** `0865a2e` | Caps: 5000 entries, 32 MB per entry, 64 MB in total. The inflate stream is counted against the declared `usize` and cancelled past it. Offsets are bounds-checked. `tests/unit/zip-limits.mjs` covers the cap, a central directory that lies about sizes, and truncation. |
| 4 | the previous user's saves are uploaded into a new account | **fixed** `0a1a36a` | `colobot.owner`. A different account gets "Upload to this account / Start fresh" before any upload. Sign-out offers to remove the saves from this browser. e2e: sign out (keep), register another account → asked → "Start fresh" leaves no `offline-test.txt` and uploads nothing. |
| 5 | `importZip` reloads over a failed or conflicting sync | **fixed** `0a1a36a` | `syncUp` returns an outcome and never rejects. Import waits for any running upload, reloads only on `synced`/`unchanged`, and leaves the reload to the conflict path on `reloading`. Otherwise it shows a toast with "Reload". |
| 6 | chunked bodies get around the size limits | **fixed** `b810539` | `readBody` counts the stream and cancels it past the cap. Test: chunked 2 MB JSON → 413, chunked 25 MB PUT → 413. |
| 7 | 100k PBKDF2 against the Free 10 ms limit | **fixed** `947997b` | Measured, 20k by default (see above). Requiring a Paid plan was not an option, because the plan is unknown. |
| 8 | `crypto.subtle.timingSafeEqual` "does not exist" | **rejected** | It is a non-standard Workers extension. Probed in workerd: `typeof === 'function'`, `([1,2,3],[1,2,3]) → true`, `([1,2,3],[1,2,4]) → false`. Kept, with the JS fallback for other runtimes and a comment (`947997b`). |
| 9 | `rate_limits` is never pruned | **fixed** `f4519e8`, `b810539` | About 1 in 16 writes deletes rows older than 2 windows (index on `window_start`). Account deletion also removes its `login-ipemail:*\|email` and `put-user:<id>` rows. |

## Fable (VERDICT OK)

| # | finding | verdict | where / evidence |
| --- | --- | --- | --- |
| F1 | PBKDF2 against Free CPU | **fixed** `947997b` | see the measurement above |
| F2 | sync state is written before the flush | **fixed** `0a1a36a` | `restoreFromCloud`: flush, then `storeSync`. A related case surfaced in the first e2e run: an upload of in-memory files that were not yet flushed, followed by a reload, re-uploaded the older IndexedDB copy as a new version. `syncUp` now flushes before it zips (its own flush does not re-trigger the debounce). |
| F3 | `colobot.json` in the sync set | **fixed** `0a1a36a` | Excluded from the archive and the hash, kept on restore, still in "Download saves". e2e: the cloud archive has no `colobot.json`, and a settings-only change → `unchanged`. `mods/` stays synced (user content). |
| F4 | two tabs | **fixed** `0a1a36a` | Web Lock `colobot-save` (held for the tab's life). A second tab shows a notice and never syncs. The sync state is re-read before every upload. e2e: first tab `secondary=false`, second `true`. Still open: IDBFS itself (each tab's `syncfs` can overwrite the other's files) is a pre-existing problem. The notice tells the player to close one tab. |
| F5 | shared browser | **fixed** `0a1a36a` | = Gemini 4 |
| F6 | inflated-size cap | **fixed** `0865a2e` | = Gemini 3 |
| F7 | malformed cookie → 500 | **fixed** `b810539` | Bad `%`-escapes are skipped. Test: `/api/me` → 200 `user:null`, `/api/saves` → 401. |
| F8 | `rate_limits` pruning and email retention | **fixed** `f4519e8`, `b810539` | = Gemini 9. Migration 0002 also drops the old `login-email:*` rows. |
| F9 | no per-user upload limit | **fixed** `b810539` | `put-user`: 60 per 15 min. Test: the 60th PUT in a window → 429. |
| F10 | bodies buffered before the size check | **fixed** `b810539` | = Gemini 6 |
| F11 | `onSaveFlushed` rejections go unhandled | **fixed** `0a1a36a` | `loader.js` adds `.catch(console.warn)`, and `syncUp` catches everything. |
| F12 | account deletion with only the cookie | **fixed** `b810539`, `0a1a36a` | `{confirm:"delete"}` is always required. Password accounts also send the password (rate-limited like login). Google accounts need a session under 10 min old, otherwise `403 reauth_required` with a "Sign in with Google" button. `privacy.html` asks for the password too. Test: no confirm → 400, wrong password → 403, correct → 200. |
| F13a | `__Host-` cookie prefix | **fixed** `b810539` | `__Host-colobot_session` (tests pass on `http://localhost` in Chrome). Existing sessions do not matter: nothing has been deployed. |
| F13b | `Origin` fallback | **fixed** `b810539` | `Sec-Fetch-Site: same-origin` is accepted when `Origin` is missing. The `Referrer-Policy` warning is in the docs. |
| F13c | `errText('constructor')` | **fixed** `0a1a36a` | `Object.hasOwn` |
| F13d | `device`/`hash` stored verbatim | **partly** `b810539` | `X-Device` is whitelisted to `[\w .()-]{0,80}`. `X-Save-Hash` stays length-capped only: it is compared, never interpreted, and rendered via `textContent`. |

## Not done / residual risk

- Credential stuffing spread across many IPs is limited per IP only, by design (the alternative is the lockout DoS). Turnstile on the login form is the next step if this is ever seen.
- The production CPU time was not measured (nothing deployed). After the deploy, run `wrangler tail` on the first sign-ins.

## Test runs (after the fixes)

- `node tests/e2e/accounts.mjs`: 18/18 PASS
- `node tests/e2e/accounts-api.mjs`: 15/15 PASS
- `node tests/e2e/save-roundtrip.mjs`: ok (`lost: []`)
- `node tests/unit/zip-limits.mjs`: ok
