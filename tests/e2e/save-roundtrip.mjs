// IDBFS round trip: create the default player (Enter on the name dialog), reload the page in the
// same browser profile and check that every path created under /save survived.
// Usage: node tests/e2e/save-roundtrip.mjs [--dist DIR]
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const i = args.indexOf('--dist');
const DIST = path.resolve(i < 0 ? path.join(root, 'dist') : args[i + 1]);
const OUT = path.join(root, 'tests/e2e/out');
fs.mkdirSync(OUT, { recursive: true });
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json' };

const server = http.createServer((req, res) => {
  const p = path.join(DIST, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  const f = p.endsWith('/') ? path.join(p, 'index.html') : p;
  if (!f.startsWith(DIST) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
  fs.createReadStream(f).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://localhost:${server.address().port}/`;

const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'colobot-save-'));
const ctx = await chromium.launchPersistentContext(profile, {
  channel: 'chrome', headless: true, viewport: { width: 1280, height: 800 },
  args: ['--enable-unsafe-webgpu', '--enable-features=WebAssemblyExperimentalJSPI', '--enable-experimental-webassembly-jspi'],
});
const page = ctx.pages()[0] || await ctx.newPage();
const ready = async () => {
  await page.waitForFunction(() => (window.__colobotFrames || 0) > 30, null, { timeout: 60000 });
  await page.waitForTimeout(2000);
};
const listSave = () => page.evaluate(() => {
  const FS = window.Module.FS, walk = (d) => FS.readdir(d).filter((n) => n !== '.' && n !== '..')
    .flatMap((n) => { const p = d + '/' + n; return FS.isDir(FS.stat(p).mode) ? [p + '/', ...walk(p)] : [p]; });
  return walk('/save');
});

await page.goto(url);
await ready();
const before = await listSave();
await page.keyboard.press('Enter'); // OK on "Player's name" -> creates /save/player/Player
await page.waitForTimeout(1500);    // > 250 ms debounce + IndexedDB commit
const afterCreate = await listSave();
await page.screenshot({ path: path.join(OUT, 'save-1-created.png') });

await page.reload();
await ready();
const afterReload = await listSave();
await page.screenshot({ path: path.join(OUT, 'save-2-reloaded.png') });

const created = afterCreate.filter((p) => !before.includes(p));
const lost = created.filter((p) => !afterReload.includes(p));
const result = { before, created, afterReload, lost, ok: created.length > 0 && lost.length === 0 };
console.log(JSON.stringify(result, null, 1));
await ctx.close(); server.close();
fs.rmSync(profile, { recursive: true, force: true });
process.exit(result.ok ? 0 : 1);
