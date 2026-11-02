// WebCrypto helpers: password hashing (PBKDF2-SHA256), random tokens, constant-time compare.
// Iterations: the Workers plan is not known, so the default must fit the Free plan's 10 ms CPU
// per request. Measured in workerd (wrangler 4.156, Apple M4 Pro): 0.098 ms per 1000 iterations
// (100k = 9.8 ms, 20k = 1.95 ms). 20k leaves room for slower production cores (x2-3) plus the
// rest of the request. The count is stored per hash (users.password_iter) and login rehashes
// older/weaker hashes, so it can be raised later with the PBKDF2_ITER var (Workers caps at 100k).
// See docs/ACCOUNTS.md "Password hashing cost".
export const PBKDF2_ITER = 20000;
export const PBKDF2_MAX = 100000;
export function iterations(env) {
  const v = Math.floor(+(env && env.PBKDF2_ITER));
  return Number.isFinite(v) && v >= 10000 ? Math.min(v, PBKDF2_MAX) : PBKDF2_ITER;
}

const enc = new TextEncoder();

export function b64(bytes) {
  let s = '';
  for (const b of new Uint8Array(bytes)) s += String.fromCharCode(b);
  return btoa(s);
}
export function unb64(str) {
  const s = atob(str), out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}
export function b64url(bytes) {
  return b64(bytes).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
export function randomBytes(n) {
  return crypto.getRandomValues(new Uint8Array(n));
}
export const randomToken = (n = 32) => b64url(randomBytes(n));

export async function sha256Hex(text) {
  const d = await crypto.subtle.digest('SHA-256', enc.encode(text));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}
export async function sha256B64url(text) {
  return b64url(await crypto.subtle.digest('SHA-256', enc.encode(text)));
}

export async function pbkdf2(password, salt, iterations = PBKDF2_ITER) {
  const key = await crypto.subtle.importKey('raw', enc.encode(password), 'PBKDF2', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations }, key, 256);
  return new Uint8Array(bits);
}

// Constant-time equality for byte arrays / strings of equal length (length itself is not secret).
// crypto.subtle.timingSafeEqual is a non-standard Workers extension (verified present in workerd:
// typeof 'function'); the XOR loop is the fallback for other runtimes (Node tests).
export function timingSafeEqual(a, b) {
  if (typeof a === 'string') a = enc.encode(a);
  if (typeof b === 'string') b = enc.encode(b);
  if (a.length !== b.length) return false;
  if (typeof crypto.subtle.timingSafeEqual === 'function') return crypto.subtle.timingSafeEqual(a, b);
  let d = 0;
  for (let i = 0; i < a.length; i++) d |= a[i] ^ b[i];
  return d === 0;
}

export async function hashPassword(password, iter = PBKDF2_ITER) {
  const salt = randomBytes(16);
  const hash = await pbkdf2(password, salt, iter);
  return { hash: b64(hash), salt: b64(salt), iter };
}

export async function verifyPassword(password, row) {
  const iter = Math.min(+row.password_iter || PBKDF2_ITER, PBKDF2_MAX);
  const got = await pbkdf2(password, unb64(row.password_salt), iter);
  return timingSafeEqual(got, unb64(row.password_hash));
}

// Burns the same PBKDF2 work for unknown emails so response time does not reveal accounts.
const DUMMY_SALT = new Uint8Array(16);
export async function dummyVerify(password, iter = PBKDF2_ITER) {
  await pbkdf2(password, DUMMY_SALT, iter);
  return false;
}
