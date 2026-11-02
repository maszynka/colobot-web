// Worker API checks against `wrangler dev` (local D1/R2 in a temp dir), no browser:
//   - failed logins for a victim's email from one IP never lock the victim out on another IP
//   - the attacker's own (IP, email) pair gets exponential backoff, not a hard lock
//   - malformed session cookie -> 401 / user null, not 500
//   - chunked bodies over the limit -> 413 (JSON and zip), without Content-Length
//   - per-user upload limit
//   - DELETE /api/me needs {confirm} and the password
// Usage: node tests/e2e/accounts-api.mjs [--port 8791]
import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const PORT = +(args[args.indexOf('--port') + 1] || 0) || 8791;
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'colobot-api-'));
const persist = path.join(tmp, 'wrangler');
const wrangler = path.join(root, 'node_modules/.bin/wrangler');
const results = [];
const check = (name, ok, info) => { results.push({ name, ok: !!ok }); console.log((ok ? 'PASS ' : 'FAIL ') + name, info === undefined ? '' : JSON.stringify(info)); };

execFileSync(wrangler, ['d1', 'migrations', 'apply', 'colobot-web', '--local', '--persist-to', persist], { cwd: root, stdio: 'ignore', env: { ...process.env, CI: '1' } });
const dev = spawn(wrangler, ['dev', '--port', String(PORT), '--ip', '127.0.0.1', '--persist-to', persist, '--show-interactive-dev-session=false'],
  { cwd: root, stdio: ['ignore', 'pipe', 'pipe'], env: { ...process.env, CI: '1' } });
let devLog = '';
dev.stdout.on('data', (d) => { devLog += d; }); dev.stderr.on('data', (d) => { devLog += d; });
const base = `http://localhost:${PORT}`;
for (let i = 0; ; i++) {
  try { if ((await fetch(base + '/api/me')).ok) break; } catch { /* starting */ }
  if (i > 60) { console.error(devLog); throw new Error('wrangler dev did not start'); }
  await new Promise((r) => setTimeout(r, 500));
}

const H = (ip, extra = {}) => ({ Origin: base, 'CF-Connecting-IP': ip, ...extra });
const post = (p, body, ip, cookie) => fetch(base + p, {
  method: 'POST', body: JSON.stringify(body),
  headers: H(ip, { 'Content-Type': 'application/json', ...(cookie ? { Cookie: cookie } : {}) }),
});
const sessionOf = (r) => (r.headers.get('Set-Cookie') || '').split(';')[0];
const EMAIL = `victim${Date.now()}@example.com`, PASS = 'correct-horse-1';

try {
  const reg = await post('/api/auth/register', { email: EMAIL, password: PASS }, '10.0.0.1');
  check('register', reg.status === 200, reg.status);
  let cookie = sessionOf(reg);
  check('session cookie uses __Host- prefix', cookie.startsWith('__Host-colobot_session='), cookie.split('=')[0]);

  // ---- lockout: 12 wrong passwords from the attacker's IP ----
  const statuses = [];
  for (let i = 0; i < 12; i++) statuses.push((await post('/api/auth/login', { email: EMAIL, password: 'wrong-' + i }, '203.0.113.9')).status);
  check('attacker gets 401 then 429 backoff', statuses.slice(0, 5).every((s) => s === 401) && statuses.includes(429), statuses);
  const blocked = await post('/api/auth/login', { email: EMAIL, password: 'wrong-x' }, '203.0.113.9');
  const retry = +blocked.headers.get('Retry-After');
  check('backoff is short (exponential, not a 15 min lock)', blocked.status === 429 && retry > 0 && retry <= 4, { status: blocked.status, retry });
  const victim = await post('/api/auth/login', { email: EMAIL, password: PASS }, '198.51.100.7');
  check('victim on another IP still signs in', victim.status === 200, victim.status);

  // ---- malformed cookie ----
  const bad = await fetch(base + '/api/me', { headers: { Cookie: '__Host-colobot_session=%E0%A4%A' } });
  check('malformed cookie -> 200 user null (not 500)', bad.status === 200 && (await bad.json()).user === null, bad.status);
  const bad2 = await fetch(base + '/api/saves', { headers: { Cookie: '__Host-colobot_session=%E0%A4%A' } });
  check('malformed cookie on /api/saves -> 401', bad2.status === 401, bad2.status);

  // ---- chunked bodies without Content-Length ----
  const chunked = (total) => new ReadableStream({
    start(c) { const part = new Uint8Array(1 << 20); for (let n = 0; n < total; n += part.length) c.enqueue(part); c.close(); },
  });
  const bigJson = await fetch(base + '/api/auth/login', {
    method: 'POST', body: chunked(2 << 20), duplex: 'half', headers: H('10.0.0.2', { 'Content-Type': 'application/json' }),
  });
  check('chunked JSON > 16 KB -> 413', bigJson.status === 413, bigJson.status);
  const bigPut = await fetch(base + '/api/saves', {
    method: 'PUT', body: chunked(25 << 20), duplex: 'half',
    headers: H('10.0.0.1', { Cookie: cookie, 'X-Colobot': '1', 'X-Base-Version': '0', 'Content-Type': 'application/zip' }),
  });
  check('chunked PUT > 20 MB -> 413', bigPut.status === 413, bigPut.status);

  // ---- per-user upload limit ----
  const empty = new Uint8Array(22); empty.set([0x50, 0x4b, 5, 6]);
  let v = 0, last = 0, puts = 0;
  for (; puts < 70; puts++) {
    const r = await fetch(base + '/api/saves', {
      method: 'PUT', body: empty,
      headers: H('10.0.0.1', { Cookie: cookie, 'X-Colobot': '1', 'X-Base-Version': String(v), 'X-Save-Hash': 'h' + puts, 'Content-Type': 'application/zip' }),
    });
    last = r.status;
    if (r.status !== 200) break;
    v = (await r.json()).saved.version;
  }
  check('per-user upload limit -> 429 before 70 uploads', last === 429 && puts < 70, { puts, last });

  // ---- DELETE /api/me needs confirmation + password ----
  const del = (body) => fetch(base + '/api/me', {
    method: 'DELETE', body: JSON.stringify(body), headers: H('10.0.0.1', { Cookie: cookie, 'X-Colobot': '1', 'Content-Type': 'application/json' }),
  });
  const d1 = await del({});
  check('DELETE without confirm -> 400', d1.status === 400, d1.status);
  const d2 = await del({ confirm: 'delete', password: 'nope-nope' });
  check('DELETE with wrong password -> 403', d2.status === 403, d2.status);
  const d3 = await del({ confirm: 'delete', password: PASS });
  check('DELETE with password -> 200', d3.status === 200, d3.status);
  const gone = await post('/api/auth/login', { email: EMAIL, password: PASS }, '198.51.100.7');
  check('deleted account cannot sign in', gone.status === 401, gone.status);
  check('no server_error in the dev log', !/api error/.test(devLog));
} catch (e) {
  check('no exception', false, String(e && e.stack || e));
} finally {
  dev.kill('SIGINT');
  fs.rmSync(tmp, { recursive: true, force: true });
}
const ok = results.every((r) => r.ok);
console.log(ok ? 'ALL PASS' : 'FAILED');
process.exit(ok ? 0 : 1);
