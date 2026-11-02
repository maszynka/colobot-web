// Accounts + cloud save sync, end to end against `wrangler dev` (local D1/R2 in a temp dir).
//   1. browser A: register (email+password) -> create the default player -> saves upload to /api/saves
//   2. "Download saves" from the account menu produces a valid zip containing the player
//   3. browser B (fresh profile): sign in -> the player from A is restored before the game reads /save
//   4. browser C: "Play locally" -> creating a player never calls /api/saves
// Usage: node tests/e2e/accounts.mjs [--headed] [--port 8790]   (needs dist/, see docs/STATUS.md)
import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';
import { readZip } from '../../web/zip.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const PORT = +(args[args.indexOf('--port') + 1] || 0) || 8790;
const OUT = path.join(root, 'tests/e2e/out');
fs.mkdirSync(OUT, { recursive: true });
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'colobot-acct-'));
const persist = path.join(tmp, 'wrangler');
const wrangler = path.join(root, 'node_modules/.bin/wrangler');

const results = [];
const check = (name, ok, info) => { results.push({ name, ok: !!ok, info }); console.log((ok ? 'PASS ' : 'FAIL ') + name, info === undefined ? '' : JSON.stringify(info)); };

execFileSync(wrangler, ['d1', 'migrations', 'apply', 'colobot-web', '--local', '--persist-to', persist], { cwd: root, stdio: 'ignore', env: { ...process.env, CI: '1' } });
const dev = spawn(wrangler, ['dev', '--port', String(PORT), '--ip', '127.0.0.1', '--persist-to', persist, '--show-interactive-dev-session=false'],
  { cwd: root, stdio: ['ignore', 'pipe', 'pipe'], env: { ...process.env, CI: '1' } });
let devLog = '';
dev.stdout.on('data', (d) => { devLog += d; }); dev.stderr.on('data', (d) => { devLog += d; });
const url = `http://localhost:${PORT}/`;
for (let i = 0; ; i++) {
  try { if ((await fetch(url + 'api/me')).ok) break; } catch { /* starting */ }
  if (i > 60) { console.error(devLog); throw new Error('wrangler dev did not start'); }
  await new Promise((r) => setTimeout(r, 500));
}

const launch = async (name) => {
  const ctx = await chromium.launchPersistentContext(path.join(tmp, name), {
    channel: 'chrome', headless: !args.includes('--headed'), viewport: { width: 1280, height: 800 }, acceptDownloads: true, locale: 'en-US',
    args: ['--enable-unsafe-webgpu', '--enable-features=WebAssemblyExperimentalJSPI', '--enable-experimental-webassembly-jspi'],
  });
  const page = ctx.pages()[0] || await ctx.newPage();
  const api = [];
  page.on('request', (r) => { if (new URL(r.url()).pathname.startsWith('/api/')) api.push(r.method() + ' ' + new URL(r.url()).pathname); });
  page.on('pageerror', (e) => fs.appendFileSync(path.join(OUT, `accounts-${name}.log`), 'PAGEERROR: ' + e.message + '\n'));
  page.on('console', (m) => fs.appendFileSync(path.join(OUT, `accounts-${name}.log`), m.type() + ': ' + m.text() + '\n'));
  return { ctx, page, api };
};
const ready = async (page) => {
  await page.waitForFunction(() => (window.__colobotFrames || 0) > 30, null, { timeout: 90000 });
  await page.waitForTimeout(1500);
};
const listSave = (page) => page.evaluate(() => {
  const FS = window.Module.FS, walk = (d) => FS.readdir(d).filter((n) => n !== '.' && n !== '..')
    .flatMap((n) => { const p = d + '/' + n; return FS.isDir(FS.stat(p).mode) ? [p + '/', ...walk(p)] : [p]; });
  return walk('/save');
});
const EMAIL = `pilot${Date.now()}@example.com`, PASS = 'houston-1234';

try {
  // ---- 1. register + first save ----
  const A = await launch('A');
  await A.page.goto(url);
  await A.page.getByRole('button', { name: 'Email and password' }).click();
  await A.page.getByRole('button', { name: 'I need an account' }).click();
  await A.page.getByPlaceholder('email').fill(EMAIL);
  await A.page.getByPlaceholder(/password/).fill(PASS);
  await A.page.getByRole('button', { name: 'Create account' }).click();
  await ready(A.page);
  check('badge shows signed-in email', (await A.page.locator('#acct-badge').textContent()).includes(EMAIL));
  const before = await listSave(A.page);
  await A.page.locator('#canvas').focus();
  await A.page.keyboard.press('Enter');                 // OK on "Player's name" -> /save/player/Player...
  await A.page.waitForTimeout(1000);
  const created = (await listSave(A.page)).filter((p) => !before.includes(p));
  check('player created in /save', created.length > 0, created);
  let latest = null;                                   // 5 s debounce after the IDBFS flush, then PUT
  for (let t = 0; t < 40 && !latest; t++) {
    await A.page.waitForTimeout(500);
    latest = await A.page.evaluate(async () => (await (await fetch('/api/saves', { cache: 'no-store' })).json()).latest);
  }
  check('save uploaded to cloud', latest, latest);

  // ---- 2. download ----
  await A.page.locator('#acct-badge').click();
  const [dl] = await Promise.all([A.page.waitForEvent('download'), A.page.getByRole('menuitem', { name: /Download saves/ }).click()]);
  const zipPath = path.join(OUT, 'accounts-download.zip');
  await dl.saveAs(zipPath);
  let zipEntries = [];
  try { zipEntries = (await readZip(new Uint8Array(fs.readFileSync(zipPath)))).map((e) => '/save/' + e.path); } catch (e) { zipEntries = ['ERR ' + e.message]; }
  check('download is a valid zip with the player', created.every((p) => zipEntries.includes(p)), { name: dl.suggestedFilename(), zipEntries });
  if (args.includes('--unzip')) execFileSync('unzip', ['-t', zipPath], { stdio: 'inherit' });
  await A.page.screenshot({ path: path.join(OUT, 'accounts-A.png') });
  await A.ctx.close();

  // ---- 3. another browser, sign in, saves restored ----
  const B = await launch('B');
  await B.page.goto(url);
  await B.page.getByRole('button', { name: 'Email and password' }).click();
  await B.page.getByPlaceholder('email').fill(EMAIL);
  await B.page.getByPlaceholder(/password/).fill(PASS);
  await B.page.getByRole('button', { name: 'Sign in' }).click();
  await ready(B.page);
  const restored = await listSave(B.page);
  check('saves restored on second browser', created.every((p) => restored.includes(p)), restored);
  await B.page.screenshot({ path: path.join(OUT, 'accounts-B.png') });
  // conflict: another device uploads v2 while B changes /save locally -> prompt -> "Use this device"
  const v2 = await B.page.evaluate(async () => {
    const empty = new Uint8Array(22); empty.set([0x50, 0x4b, 5, 6]);
    const r = await fetch('/api/saves', { method: 'PUT', body: empty, headers: { 'X-Colobot': '1', 'X-Base-Version': '1', 'X-Save-Hash': 'other-device', 'Content-Type': 'application/zip' } });
    return r.status;
  });
  await B.page.evaluate(() => { window.Module.FS.writeFile('/save/conflict-test.txt', 'from B'); window.__colobotAccount.syncNow(); });
  const prompt = await B.page.getByText('Two different versions of your saves').waitFor({ timeout: 10000 }).then(() => true, () => false);
  check('conflict prompt shown', v2 === 200 && prompt, { v2 });
  if (prompt) await B.page.getByRole('button', { name: 'Use this device' }).click();
  let v3 = null;
  for (let t = 0; t < 20 && !(v3 && v3.version === 3); t++) {
    await B.page.waitForTimeout(500);
    v3 = await B.page.evaluate(async () => (await (await fetch('/api/saves', { cache: 'no-store' })).json()).latest);
  }
  check('"Use this device" uploads a new version', v3 && v3.version === 3 && v3.hash !== 'other-device', v3);
  // reload: stays signed in, no prompt, no new upload needed
  await B.page.reload();
  await ready(B.page);
  check('session survives reload (no chooser)', (await B.page.locator('.acct-modal').count()) === 0);
  await B.ctx.close();

  // ---- 4. local mode never touches /api/saves ----
  const C = await launch('C');
  await C.page.goto(url);
  await C.page.getByRole('button', { name: /Play locally/ }).click();
  await ready(C.page);
  await C.page.locator('#canvas').focus();
  await C.page.keyboard.press('Enter');
  await C.page.waitForTimeout(7000);                    // > 5 s cloud debounce
  check('local mode created a player', (await listSave(C.page)).length > 0);
  check('local mode never calls /api/saves', !C.api.some((r) => r.includes('/api/saves')), C.api);
  await C.page.reload();
  await ready(C.page);
  check('local choice remembered (no chooser on reload)', (await C.page.locator('.acct-modal').count()) === 0);
  await C.ctx.close();
} catch (e) {
  check('no exception', false, String(e && e.stack || e));
} finally {
  dev.kill('SIGINT');
  fs.writeFileSync(path.join(OUT, 'accounts-wrangler.log'), devLog);
  fs.rmSync(tmp, { recursive: true, force: true });
}
const ok = results.every((r) => r.ok);
console.log(ok ? 'ALL PASS' : 'FAILED');
process.exit(ok ? 0 : 1);
