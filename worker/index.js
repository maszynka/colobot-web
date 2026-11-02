// Colobot web Worker: static assets (ASSETS) + /api/* (accounts and cloud saves).
// Architecture, endpoints and setup: docs/ACCOUNTS.md.
import {
  hashPassword, verifyPassword, dummyVerify, iterations, randomToken, sha256Hex, sha256B64url, timingSafeEqual,
} from './crypto.js';

// __Host- prefix: the browser rejects it unless Secure, Path=/ and no Domain (no subdomain override).
const SESSION_COOKIE = '__Host-colobot_session';
const OAUTH_COOKIE = 'colobot_oauth';
const SESSION_TTL = 30 * 24 * 3600;           // 30 days
const MAX_SAVE_BYTES = 20 * 1024 * 1024;      // 20 MB per archive
const KEEP_VERSIONS = 10;
const WINDOW = 15 * 60;
// Fixed windows: at most `max` events per WINDOW per key.
//   login-ip      failed logins from one IP (any email)
//   register-ip   registrations from one IP
//   put-user      save uploads per account
// Login per (IP, email) uses exponential backoff instead (see loginBackoff), so nobody can lock a
// victim out by knowing only the email: the counter is never keyed by email alone.
const LIMITS = { 'login-ip': 30, 'register-ip': 10, 'put-user': 60 };
const BACKOFF_FREE = 5;                       // failures per (IP, email) before any delay
const BACKOFF_MAX = WINDOW;                   // delay cap; counters reset after WINDOW of quiet

const now = () => Math.floor(Date.now() / 1000);

class HttpError extends Error {
  constructor(status, code, extra) { super(code); this.status = status; this.code = code; this.extra = extra; }
}

function json(data, status = 200, headers = {}) {
  return new Response(JSON.stringify(data), {
    status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...headers },
  });
}

function parseCookies(req) {
  const out = {};
  for (const part of (req.headers.get('Cookie') || '').split(';')) {
    const i = part.indexOf('=');
    if (i <= 0) continue;
    // A malformed value (bad %-escape) is skipped, not a 500 for every request.
    try { out[part.slice(0, i).trim()] = decodeURIComponent(part.slice(i + 1).trim()); } catch { /* skip */ }
  }
  return out;
}

function cookie(name, value, { maxAge, path = '/' } = {}) {
  let c = `${name}=${encodeURIComponent(value)}; Path=${path}; HttpOnly; Secure; SameSite=Lax`;
  if (maxAge !== undefined) c += `; Max-Age=${maxAge}`;
  return c;
}

const clientIp = (req) => req.headers.get('CF-Connecting-IP') || 'local';

// ---------- CSRF: same-origin Origin on every state-changing request ----------
function checkCsrf(req, url) {
  if (req.method === 'GET' || req.method === 'HEAD') return;
  const origin = req.headers.get('Origin');
  // Fallback for the rare same-origin client that omits Origin: Sec-Fetch-Site (browser-set).
  const sameSite = !origin && req.headers.get('Sec-Fetch-Site') === 'same-origin';
  if (!sameSite && origin !== url.origin) throw new HttpError(403, 'bad_origin');
  const ct = (req.headers.get('Content-Type') || '').split(';')[0].trim();
  if (req.method === 'POST' && ct !== 'application/json') throw new HttpError(415, 'json_required');
  // PUT (zip body) and DELETE carry a custom header, which a cross-site form cannot send.
  if ((req.method === 'PUT' || req.method === 'DELETE') && req.headers.get('X-Colobot') !== '1')
    throw new HttpError(403, 'missing_header');
}

// Reads the body as bytes, aborting as soon as it passes `max` (Content-Length is only a hint:
// a chunked body has none, so the stream itself is counted).
async function readBody(req, max) {
  const declared = +(req.headers.get('Content-Length') || 0);
  if (declared > max) throw new HttpError(413, 'too_large', { maxBytes: max });
  if (!req.body) return new Uint8Array(0);
  const reader = req.body.getReader(), chunks = [];
  let total = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > max) {
      reader.cancel().catch(() => {});
      throw new HttpError(413, 'too_large', { maxBytes: max });
    }
    chunks.push(value);
  }
  const out = new Uint8Array(total);
  let o = 0;
  for (const c of chunks) { out.set(c, o); o += c.byteLength; }
  return out;
}

async function readJson(req) {
  const bytes = await readBody(req, 16384);
  try { return JSON.parse(new TextDecoder().decode(bytes)); } catch { throw new HttpError(400, 'bad_json'); }
}

// ---------- rate limiting (D1 table rate_limits) ----------
async function rateCheck(env, kind, id) {
  const key = `${kind}:${id}`, t = now();
  const row = await env.DB.prepare('SELECT count, window_start FROM rate_limits WHERE key = ?').bind(key).first();
  if (row && t - row.window_start < WINDOW && row.count >= LIMITS[kind])
    throw new HttpError(429, 'too_many_attempts', { retryAfter: WINDOW - (t - row.window_start) });
}
async function rateHit(env, kind, id) {
  const key = `${kind}:${id}`, t = now();
  await env.DB.prepare(
    `INSERT INTO rate_limits (key, count, window_start) VALUES (?1, 1, ?2)
     ON CONFLICT(key) DO UPDATE SET
       count = CASE WHEN ?2 - window_start >= ?3 THEN 1 ELSE count + 1 END,
       window_start = CASE WHEN ?2 - window_start >= ?3 THEN ?2 ELSE window_start END`,
  ).bind(key, t, WINDOW).run();
  await ratePrune(env, t);
}
const rateClear = (env, key) => env.DB.prepare('DELETE FROM rate_limits WHERE key = ?').bind(key).run();

// Opportunistic cleanup (about 1 in 16 writes): rows whose window ended are useless.
async function ratePrune(env, t, force = false) {
  if (!force && Math.random() >= 1 / 16) return;
  await env.DB.prepare('DELETE FROM rate_limits WHERE window_start < ?').bind(t - 2 * WINDOW).run();
}

// Login backoff per (IP, email): the first BACKOFF_FREE failures are free, then each further
// attempt must wait 2^(n - BACKOFF_FREE) s (2, 4, 8 ... capped at BACKOFF_MAX) after the last
// failure. window_start holds the time of the last failure; WINDOW of quiet resets the counter.
const loginKey = (ip, email) => `login-ipemail:${ip}|${email}`;
function backoffDelay(count) {
  return count < BACKOFF_FREE ? 0 : Math.min(BACKOFF_MAX, 2 ** (count - BACKOFF_FREE + 1));
}
async function loginBackoffCheck(env, ip, email) {
  const t = now();
  const row = await env.DB.prepare('SELECT count, window_start FROM rate_limits WHERE key = ?').bind(loginKey(ip, email)).first();
  if (!row || t - row.window_start >= WINDOW) return;
  const wait = backoffDelay(row.count) - (t - row.window_start);
  if (wait > 0) throw new HttpError(429, 'too_many_attempts', { retryAfter: wait });
}
async function loginBackoffHit(env, ip, email) {
  const t = now();
  await env.DB.prepare(
    `INSERT INTO rate_limits (key, count, window_start) VALUES (?1, 1, ?2)
     ON CONFLICT(key) DO UPDATE SET
       count = CASE WHEN ?2 - window_start >= ?3 THEN 1 ELSE count + 1 END,
       window_start = ?2`,
  ).bind(loginKey(ip, email), t, WINDOW).run();
}

// ---------- sessions ----------
async function createSession(env, userId) {
  const token = randomToken(32), t = now();
  await env.DB.prepare('INSERT INTO sessions (token_hash, user_id, created_at, expires_at) VALUES (?, ?, ?, ?)')
    .bind(await sha256Hex(token), userId, t, t + SESSION_TTL).run();
  // Opportunistic cleanup of expired rows.
  await env.DB.prepare('DELETE FROM sessions WHERE expires_at < ?').bind(t).run();
  return cookie(SESSION_COOKIE, token, { maxAge: SESSION_TTL });
}

async function currentUser(req, env) {
  const token = parseCookies(req)[SESSION_COOKIE];
  if (!token || token.length > 100) return null;
  const row = await env.DB.prepare(
    `SELECT u.id, u.email, u.password_hash, u.password_salt, u.password_iter, s.token_hash, s.created_at
     FROM sessions s JOIN users u ON u.id = s.user_id
     WHERE s.token_hash = ? AND s.expires_at > ?`,
  ).bind(await sha256Hex(token), now()).first();
  if (!row) return null;
  const g = await env.DB.prepare("SELECT email FROM oauth_identities WHERE user_id = ? AND provider = 'google'")
    .bind(row.id).first();
  return {
    id: row.id, email: row.email || (g && g.email) || null, provider: row.email ? 'password' : 'google',
    tokenHash: row.token_hash, sessionAge: now() - row.created_at, row,
  };
}

async function requireUser(req, env) {
  const u = await currentUser(req, env);
  if (!u) throw new HttpError(401, 'not_signed_in');
  return u;
}

const publicUser = (u) => u && { id: u.id, email: u.email, provider: u.provider };
const googleConfigured = (env) => !!(env.GOOGLE_CLIENT_ID && env.GOOGLE_CLIENT_SECRET);

// ---------- email + password ----------
const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
function validCreds(body) {
  const email = typeof body.email === 'string' ? body.email.trim().toLowerCase() : '';
  const password = typeof body.password === 'string' ? body.password : '';
  if (!EMAIL_RE.test(email) || email.length > 254) throw new HttpError(400, 'bad_email');
  if (password.length < 8 || password.length > 200) throw new HttpError(400, 'bad_password');
  return { email, password };
}

async function register(req, env) {
  const ip = clientIp(req);
  await rateCheck(env, 'register-ip', ip);
  const { email, password } = validCreds(await readJson(req));
  await rateHit(env, 'register-ip', ip);
  const exists = await env.DB.prepare('SELECT 1 FROM users WHERE email = ?').bind(email).first();
  if (exists) throw new HttpError(409, 'email_taken');
  const ph = await hashPassword(password, iterations(env));
  const id = crypto.randomUUID();
  try {
    await env.DB.prepare(
      'INSERT INTO users (id, email, password_hash, password_salt, password_iter, created_at) VALUES (?, ?, ?, ?, ?, ?)',
    ).bind(id, email, ph.hash, ph.salt, ph.iter, now()).run();
  } catch (e) {
    if (String(e).includes('UNIQUE')) throw new HttpError(409, 'email_taken');
    throw e;
  }
  const c = await createSession(env, id);
  return json({ user: { id, email, provider: 'password' } }, 200, { 'Set-Cookie': c });
}

async function login(req, env) {
  const ip = clientIp(req);
  const body = await readJson(req);
  const email = typeof body.email === 'string' ? body.email.trim().toLowerCase().slice(0, 254) : '';
  const password = typeof body.password === 'string' ? body.password.slice(0, 200) : '';
  await rateCheck(env, 'login-ip', ip);
  await loginBackoffCheck(env, ip, email);
  const row = email && await env.DB.prepare(
    'SELECT id, email, password_hash, password_salt, password_iter FROM users WHERE email = ?',
  ).bind(email).first();
  const iter = iterations(env);
  const ok = row && row.password_hash ? await verifyPassword(password, row) : await dummyVerify(password, iter);
  if (!ok) {
    await rateHit(env, 'login-ip', ip);
    await loginBackoffHit(env, ip, email);
    throw new HttpError(401, 'bad_credentials');
  }
  await rateClear(env, loginKey(ip, email));
  if (row.password_iter !== iter) {
    // Re-hash at the current cost (raised or lowered via PBKDF2_ITER); stored per user.
    const ph = await hashPassword(password, iter);
    await env.DB.prepare('UPDATE users SET password_hash = ?, password_salt = ?, password_iter = ? WHERE id = ?')
      .bind(ph.hash, ph.salt, ph.iter, row.id).run();
  }
  const c = await createSession(env, row.id);
  return json({ user: { id: row.id, email: row.email, provider: 'password' } }, 200, { 'Set-Cookie': c });
}

async function logout(req, env) {
  const u = await currentUser(req, env);
  if (u) await env.DB.prepare('DELETE FROM sessions WHERE token_hash = ?').bind(u.tokenHash).run();
  return json({ ok: true }, 200, { 'Set-Cookie': cookie(SESSION_COOKIE, '', { maxAge: 0 }) });
}

// Irreversible, so a session cookie alone is not enough (left-behind / stolen session):
//   body {confirm: "delete"} always; password accounts also send {password};
//   Google accounts need a session younger than REAUTH_AGE (sign in with Google again).
const REAUTH_AGE = 10 * 60;
async function deleteAccount(req, env) {
  const u = await requireUser(req, env);
  const body = await readJson(req);
  if (!body || body.confirm !== 'delete') throw new HttpError(400, 'confirm_required');
  if (u.provider === 'password') {
    const ip = clientIp(req);
    await rateCheck(env, 'login-ip', ip);
    await loginBackoffCheck(env, ip, u.email);
    const password = typeof body.password === 'string' ? body.password.slice(0, 200) : '';
    if (!password || !(await verifyPassword(password, u.row))) {
      await rateHit(env, 'login-ip', ip);
      await loginBackoffHit(env, ip, u.email);
      throw new HttpError(403, 'bad_credentials');
    }
  } else if (u.sessionAge > REAUTH_AGE) {
    throw new HttpError(403, 'reauth_required', { maxAge: REAUTH_AGE });
  }
  const rows = await env.DB.prepare('SELECT r2_key FROM saves WHERE user_id = ?').bind(u.id).all();
  const keys = rows.results.map((r) => r.r2_key);
  if (keys.length) await env.SAVES.delete(keys);
  await env.DB.batch([
    env.DB.prepare('DELETE FROM saves WHERE user_id = ?').bind(u.id),
    env.DB.prepare('DELETE FROM sessions WHERE user_id = ?').bind(u.id),
    env.DB.prepare('DELETE FROM oauth_identities WHERE user_id = ?').bind(u.id),
    env.DB.prepare('DELETE FROM users WHERE id = ?').bind(u.id),
    // Login counters keyed by this email ('login-ipemail:<ip>|<email>') go too.
    env.DB.prepare("DELETE FROM rate_limits WHERE key LIKE 'login-ipemail:%' AND substr(key, -length(?1)) = ?1")
      .bind(u.provider === 'password' ? '|' + u.email : '\u0000'),
    env.DB.prepare('DELETE FROM rate_limits WHERE key = ?').bind('put-user:' + u.id),
  ]);
  return json({ deleted: true }, 200, { 'Set-Cookie': cookie(SESSION_COOKIE, '', { maxAge: 0 }) });
}

// ---------- Google OAuth 2.0 (authorization code + PKCE + state) ----------
const redirectUri = (env, url) => env.GOOGLE_REDIRECT_URI || `${url.origin}/api/auth/google/callback`;
const back = (url, q) => Response.redirect(`${url.origin}/?${q}`, 302);

async function googleStart(req, env, url) {
  if (!googleConfigured(env)) return back(url, 'auth_error=google_not_configured');
  const state = randomToken(24), verifier = randomToken(48);
  const p = new URLSearchParams({
    client_id: env.GOOGLE_CLIENT_ID,
    redirect_uri: redirectUri(env, url),
    response_type: 'code',
    scope: 'openid email',
    state,
    code_challenge: await sha256B64url(verifier),
    code_challenge_method: 'S256',
    prompt: 'select_account',
  });
  return new Response(null, {
    status: 302,
    headers: {
      Location: 'https://accounts.google.com/o/oauth2/v2/auth?' + p,
      'Set-Cookie': cookie(OAUTH_COOKIE, `${state}.${verifier}`, { maxAge: 600, path: '/api/auth/google' }),
      'Cache-Control': 'no-store',
    },
  });
}

function decodeJwtPayload(jwt) {
  const part = jwt.split('.')[1].replace(/-/g, '+').replace(/_/g, '/');
  return JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(part), (c) => c.charCodeAt(0))));
}

async function googleCallback(req, env, url) {
  const clear = cookie(OAUTH_COOKIE, '', { maxAge: 0, path: '/api/auth/google' });
  const fail = (code) => new Response(null, {
    status: 302, headers: { Location: `${url.origin}/?auth_error=${code}`, 'Set-Cookie': clear },
  });
  if (!googleConfigured(env)) return fail('google_not_configured');
  if (url.searchParams.get('error')) return fail('google_denied');
  const [state, verifier] = (parseCookies(req)[OAUTH_COOKIE] || '').split('.');
  const gotState = url.searchParams.get('state') || '', code = url.searchParams.get('code');
  if (!state || !verifier || !code || !timingSafeEqual(state, gotState)) return fail('google_state');

  // The ID token comes straight from Google's token endpoint over TLS with our client secret,
  // so per OIDC Core 3.1.3.7 its signature need not be re-verified; aud/iss/exp still are.
  const tr = await fetch('https://oauth2.googleapis.com/token', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({
      code, code_verifier: verifier, grant_type: 'authorization_code',
      client_id: env.GOOGLE_CLIENT_ID, client_secret: env.GOOGLE_CLIENT_SECRET, redirect_uri: redirectUri(env, url),
    }),
  });
  if (!tr.ok) { console.warn('google token exchange', tr.status, await tr.text()); return fail('google_token'); }
  const tok = await tr.json();
  let claims;
  try { claims = decodeJwtPayload(tok.id_token); } catch { return fail('google_token'); }
  const issOk = claims.iss === 'https://accounts.google.com' || claims.iss === 'accounts.google.com';
  if (!issOk || claims.aud !== env.GOOGLE_CLIENT_ID || !(claims.exp > now()) || !claims.sub) return fail('google_token');

  const email = claims.email_verified ? claims.email : null;
  let ident = await env.DB.prepare("SELECT user_id FROM oauth_identities WHERE provider = 'google' AND subject = ?")
    .bind(claims.sub).first();
  let userId = ident && ident.user_id;
  if (!userId) {
    // Google accounts are separate from email+password accounts (no auto-linking by email,
    // which would let someone pre-register a victim's address and later read their saves).
    userId = crypto.randomUUID();
    await env.DB.batch([
      env.DB.prepare('INSERT INTO users (id, email, created_at) VALUES (?, NULL, ?)').bind(userId, now()),
      env.DB.prepare("INSERT INTO oauth_identities (provider, subject, user_id, email, created_at) VALUES ('google', ?, ?, ?, ?)")
        .bind(claims.sub, userId, email, now()),
    ]);
  } else if (email) {
    await env.DB.prepare("UPDATE oauth_identities SET email = ? WHERE provider = 'google' AND subject = ?")
      .bind(email, claims.sub).run();
  }
  const session = await createSession(env, userId);
  const h = new Headers({ Location: `${url.origin}/?signed_in=google`, 'Cache-Control': 'no-store' });
  h.append('Set-Cookie', clear);
  h.append('Set-Cookie', session);
  return new Response(null, { status: 302, headers: h });
}

// ---------- saves ----------
const saveMeta = (r) => r && { version: r.version, size: r.size, hash: r.hash, device: r.device, createdAt: r.created_at };

async function latestSave(env, userId) {
  return env.DB.prepare('SELECT * FROM saves WHERE user_id = ? ORDER BY version DESC LIMIT 1').bind(userId).first();
}

async function listSaves(req, env) {
  const u = await requireUser(req, env);
  const rows = await env.DB.prepare('SELECT * FROM saves WHERE user_id = ? ORDER BY version DESC').bind(u.id).all();
  const versions = rows.results.map(saveMeta);
  return json({ latest: versions[0] || null, versions, maxBytes: MAX_SAVE_BYTES });
}

async function getSave(req, env, which) {
  const u = await requireUser(req, env);
  const row = which === 'latest' ? await latestSave(env, u.id)
    : await env.DB.prepare('SELECT * FROM saves WHERE user_id = ? AND version = ?').bind(u.id, +which).first();
  if (!row) throw new HttpError(404, 'no_save');
  const obj = await env.SAVES.get(row.r2_key);
  if (!obj) throw new HttpError(404, 'no_save');
  return new Response(obj.body, {
    headers: {
      'Content-Type': 'application/zip',
      'Content-Disposition': `attachment; filename="colobot-save-v${row.version}.zip"`,
      'Cache-Control': 'no-store',
      'X-Save-Version': String(row.version),
      'X-Save-Hash': row.hash || '',
    },
  });
}

async function putSave(req, env) {
  const u = await requireUser(req, env);
  const declared = +(req.headers.get('Content-Length') || 0);
  if (declared > MAX_SAVE_BYTES) throw new HttpError(413, 'too_large', { maxBytes: MAX_SAVE_BYTES });
  await rateCheck(env, 'put-user', u.id);
  await rateHit(env, 'put-user', u.id);
  const baseHdr = req.headers.get('X-Base-Version') || '0';
  const base = /^\d+$/.test(baseHdr) ? +baseHdr : -1;
  if (base < 0) throw new HttpError(400, 'bad_base');
  const hash = (req.headers.get('X-Save-Hash') || '').slice(0, 128);
  const device = (req.headers.get('X-Device') || '').replace(/[^\w .()-]/g, '').slice(0, 80);

  const latest = await latestSave(env, u.id);
  const latestV = latest ? latest.version : 0;
  if (latestV !== base) throw new HttpError(409, 'conflict', { latest: saveMeta(latest) });

  const body = await readBody(req, MAX_SAVE_BYTES);
  const isZip = body.length >= 4 && body[0] === 0x50 && body[1] === 0x4b && (body[2] === 3 || body[2] === 5);
  if (!isZip) throw new HttpError(400, 'not_zip');

  const version = latestV + 1;
  const key = `u/${u.id}/${version}-${randomToken(6)}.zip`;
  await env.SAVES.put(key, body, { httpMetadata: { contentType: 'application/zip' } });
  try {
    await env.DB.prepare(
      'INSERT INTO saves (user_id, version, r2_key, size, hash, device, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
    ).bind(u.id, version, key, body.length, hash, device, now()).run();
  } catch (e) {
    await env.SAVES.delete(key);
    // A concurrent upload took this version number first.
    if (String(e).includes('UNIQUE')) throw new HttpError(409, 'conflict', { latest: saveMeta(await latestSave(env, u.id)) });
    throw e;
  }
  // Keep the newest KEEP_VERSIONS.
  const old = await env.DB.prepare('SELECT version, r2_key FROM saves WHERE user_id = ? AND version <= ?')
    .bind(u.id, version - KEEP_VERSIONS).all();
  if (old.results.length) {
    await env.SAVES.delete(old.results.map((r) => r.r2_key));
    await env.DB.prepare('DELETE FROM saves WHERE user_id = ? AND version <= ?').bind(u.id, version - KEEP_VERSIONS).run();
  }
  return json({ saved: saveMeta({ version, size: body.length, hash, device, created_at: now() }) });
}

// ---------- router ----------
async function api(req, env, url) {
  checkCsrf(req, url);
  const p = url.pathname, m = req.method;
  if (p === '/api/me' && m === 'GET') {
    const u = await currentUser(req, env);
    return json({ user: publicUser(u), google: googleConfigured(env) });
  }
  if (p === '/api/me' && m === 'DELETE') return deleteAccount(req, env);
  if (p === '/api/auth/register' && m === 'POST') return register(req, env);
  if (p === '/api/auth/login' && m === 'POST') return login(req, env);
  if (p === '/api/auth/logout' && m === 'POST') return logout(req, env);
  if (p === '/api/auth/google/start' && m === 'GET') return googleStart(req, env, url);
  if (p === '/api/auth/google/callback' && m === 'GET') return googleCallback(req, env, url);
  if (p === '/api/saves' && m === 'GET') return listSaves(req, env);
  if (p === '/api/saves' && m === 'PUT') return putSave(req, env);
  const v = p.match(/^\/api\/saves\/(latest|\d+)$/);
  if (v && m === 'GET') return getSave(req, env, v[1]);
  throw new HttpError(404, 'not_found');
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (!url.pathname.startsWith('/api/')) return env.ASSETS.fetch(req);
    try {
      return await api(req, env, url);
    } catch (e) {
      if (e instanceof HttpError) {
        const h = e.extra && e.extra.retryAfter ? { 'Retry-After': String(e.extra.retryAfter) } : {};
        return json({ error: e.code, ...(e.extra || {}) }, e.status, h);
      }
      console.error('api error', url.pathname, e && e.stack || e);
      return json({ error: 'server_error' }, 500);
    }
  },
};
