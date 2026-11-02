// WebCrypto helpers: password hashing (PBKDF2-SHA256), random tokens, constant-time compare.
// Workers cap PBKDF2 at 100 000 iterations, which is also our target.
export const PBKDF2_ITER = 100000;

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
export function timingSafeEqual(a, b) {
  if (typeof a === 'string') a = enc.encode(a);
  if (typeof b === 'string') b = enc.encode(b);
  if (a.length !== b.length) return false;
  if (crypto.subtle.timingSafeEqual) return crypto.subtle.timingSafeEqual(a, b);
  let d = 0;
  for (let i = 0; i < a.length; i++) d |= a[i] ^ b[i];
  return d === 0;
}

export async function hashPassword(password) {
  const salt = randomBytes(16);
  const hash = await pbkdf2(password, salt, PBKDF2_ITER);
  return { hash: b64(hash), salt: b64(salt), iter: PBKDF2_ITER };
}

export async function verifyPassword(password, row) {
  const got = await pbkdf2(password, unb64(row.password_salt), row.password_iter);
  return timingSafeEqual(got, unb64(row.password_hash));
}

// Burns the same PBKDF2 work for unknown emails so response time does not reveal accounts.
const DUMMY_SALT = new Uint8Array(16);
export async function dummyVerify(password) {
  await pbkdf2(password, DUMMY_SALT, PBKDF2_ITER);
  return false;
}
