#!/usr/bin/env node
// E2E boot harness: serves dist/ on localhost, opens Chrome with WebGPU, captures console,
// network and screenshots into tests/e2e/out/.
// Usage: node tests/e2e/boot.mjs [--dist DIR] [--headed] [--wait SECONDS] [--click] [--query QS] [--steps 'x,y,ms;...']
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i < 0 ? d : args[i + 1]; };
const DIST = path.resolve(opt('dist', path.join(root, 'dist')));
const WAIT = +opt('wait', '25') * 1000;
const OUT = path.join(root, 'tests/e2e/out');
fs.mkdirSync(OUT, { recursive: true });

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
  '.json': 'application/json', '.pak': 'application/octet-stream', '.css': 'text/css', '.png': 'image/png' };
const server = http.createServer((req, res) => {
  const p = path.join(DIST, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  const f = p.endsWith('/') ? path.join(p, 'index.html') : p;
  if (!f.startsWith(DIST) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { console.error('404', req.url); res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream',
    'Content-Length': fs.statSync(f).size, 'Cache-Control': 'no-store',
    'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  fs.createReadStream(f).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://localhost:${server.address().port}/`;

const browser = await chromium.launch({
  channel: 'chrome', headless: !args.includes('--headed'),
  args: ['--enable-unsafe-webgpu', '--enable-features=WebAssemblyExperimentalJSPI', '--enable-experimental-webassembly-jspi'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const log = [];
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(2) + 's';
page.on('console', (m) => log.push(`[${stamp()}] ${m.type()}: ${m.text()}`));
page.on('pageerror', (e) => log.push(`[${stamp()}] PAGEERROR: ${e.stack || e}`));
const net = [];
page.on('requestfinished', async (r) => {
  const resp = await r.response().catch(() => null);
  const len = resp ? +(await resp.headerValue('content-length').catch(() => 0)) || 0 : 0;
  net.push({ t: stamp(), url: r.url().replace(url, ''), status: resp && resp.status(), bytes: len });
});
page.on('response', (r) => { if (r.status() >= 400) log.push(`[${stamp()}] HTTP ${r.status()} ${r.url()}`); });
page.on('requestfailed', (r) => net.push({ t: stamp(), url: r.url().replace(url, ''), failed: r.failure()?.errorText }));

await page.goto(url + (opt('query', '') ? '?' + opt('query', '') : ''));
const caps = await page.evaluate(async () => ({
  gpu: !!navigator.gpu, adapter: !!(navigator.gpu && await navigator.gpu.requestAdapter()),
  jspi: typeof WebAssembly.Suspending === 'function',
}));
log.push(`[caps] ${JSON.stringify(caps)}`);

let shot = 0;
const snap = async (name) => page.screenshot({ path: path.join(OUT, `${String(shot++).padStart(2, '0')}-${name}.png`) });
let firstPaint = null, menu = null;
const deadline = Date.now() + WAIT;
while (Date.now() < deadline) {
  await page.waitForTimeout(1000);
  const st = await page.evaluate(() => ({
    loading: getComputedStyle(document.getElementById('loading')).display,
    status: document.getElementById('status').textContent,
    frames: window.__colobotFrames || 0,
  })).catch(() => null);
  if (st && st.loading === 'none' && firstPaint === null) { firstPaint = Date.now() - t0; await snap('first-paint'); }
  if (st && st.frames > 60 && menu === null) { menu = Date.now() - t0; await snap('menu'); }
}
await snap('final');
const click = async (x, y) => { await page.mouse.move(x, y); await page.waitForTimeout(200); await page.mouse.down(); await page.waitForTimeout(120); await page.mouse.up(); };
if (args.includes('--click')) {
  // CScreenWelcome (3 x 3 s, each swallows the first input) must be over: run with --wait >= 10.
  // Click OK on the player-name dialog -> main menu.
  await click(858, 240); await page.waitForTimeout(2500); await snap('main-menu');
}
for (const step of (opt('steps', '') || '').split(';').filter(Boolean)) {
  const [x, y, w] = step.split(',').map(Number); await click(x, y); await page.waitForTimeout(w || 2000); await snap(`step-${x}-${y}`);
}

const fsCheck = await page.evaluate(() => {
  try { const M = window.Module; if (!M || !M.FS) return 'no FS export';
    M.FS.writeFile('/save/_e2e_probe', 'ok'); const r = M.FS.readFile('/save/_e2e_probe', { encoding: 'utf8' });
    M.FS.unlink('/save/_e2e_probe'); return 'save writable: ' + r; } catch (e) { return 'save error: ' + e; }
});
const audio = await page.evaluate(() => {
  const c = window.Module && window.Module.AL && window.Module.AL.contexts;
  return c ? Object.values(c).map((x) => x && x.audioCtx && x.audioCtx.state).join(',') : 'no AL contexts';
});
const total = net.reduce((a, n) => a + (n.bytes > 0 ? n.bytes : 0), 0);
const frames = await page.evaluate(() => window.__colobotFrames || 0);
const summary = { url, caps, frames, firstPaintMs: firstPaint, menuMs: menu, bytes: total, requests: net.length, fsCheck, audio };
fs.writeFileSync(path.join(OUT, 'console.log'), log.join('\n') + '\n');
fs.writeFileSync(path.join(OUT, 'network.json'), JSON.stringify(net, null, 1));
fs.writeFileSync(path.join(OUT, 'summary.json'), JSON.stringify(summary, null, 1));
console.log(JSON.stringify(summary, null, 1));
await browser.close();
server.close();
