// Colobot web loader. Contract with the wasm side:
//   Module.bootPak      Uint8Array of data/boot.pak (WP11 reads it)
//   Module.dataBaseUrl  'data/' (manifest.json and loose files/packs live there)
//   Module.canvas       <canvas id="canvas">
//   Exports used: _colobot_frame() -> int (0 = quit), callMain([])
//   Save hooks (web_archiver.c -> account.js): Module.onSaveMounted(), Module.onSaveFlushed()
import { startAccount } from './account.js';

const $ = (id) => document.getElementById(id);
const setStatus = (t) => { $('status').textContent = t; };
const setProgress = (f) => { $('fill').style.width = Math.round(Math.min(1, f) * 100) + '%'; };

function unsupported(reason) {
  $('loading').style.display = 'none';
  $('canvas').style.display = 'none';
  $('reason').textContent = reason;
  $('unsupported').style.display = 'flex';
}

if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('sw.js').catch(() => {});
}

// Progress is read from a clone so the original native Response goes to
// WebAssembly.instantiateStreaming untouched (streaming compile + code cache work; a JS-wrapped
// stream defeated both).
async function fetchProgress(url, onBytes) {
  const resp = await fetch(url);
  if (!resp.ok) throw new Error(url + ': HTTP ' + resp.status);
  const total = +resp.headers.get('content-length') || 0;
  const reader = resp.clone().body.getReader();
  (async () => {
    let got = 0;
    for (;;) { const { done, value } = await reader.read(); if (done) break; got += value.length; onBytes(got, total); }
  })().catch(() => {});
  return resp;
}

function fail(e) {
  console.error(e);
  $('loading').style.display = 'flex';
  setStatus('Error: ' + (e && e.message || e));
}

async function main() {
  if (!navigator.gpu) return unsupported('WebGPU (navigator.gpu) is not available.');
  if (typeof WebAssembly === 'undefined' || !WebAssembly.Suspending || !WebAssembly.promising)
    return unsupported('WebAssembly JSPI (WebAssembly.Suspending) is not available.');

  // Sign-in / local choice runs in parallel with the download; the game waits for it only
  // when it mounts /save (onSaveMounted), so cloud saves can be restored before they are read.
  const account = startAccount();

  const progress = { wasm: [0, 1], pak: [0, 1] };
  const update = () => {
    const got = progress.wasm[0] + progress.pak[0];
    const tot = progress.wasm[1] + progress.pak[1];
    setProgress(got / tot);
    setStatus('Downloading… ' + (got / 1048576).toFixed(1) + ' MB');
  };

  // Start everything in parallel.
  const factoryP = import('./colobot.js');
  const wasmRespP = fetchProgress('colobot.wasm', (g, t) => { progress.wasm = [g, t || g + 1]; update(); });
  // boot.pak.gz (gzip, inflated here: .pak is octet-stream, which the CDN does not compress);
  // falls back to the raw pack.
  const onPak = (g, t) => { progress.pak = [g, t || g + 1]; update(); };
  // Data URLs are content-addressed (boot.<sha>.pak.gz); assemble-dist.sh writes the current
  // name into the <link data-boot> preload so the fetch starts before manifest.json arrives.
  const manifestP = fetch('data/manifest.json', { cache: 'no-cache' }).then((r) => r.json());
  const inflate = (r) => new Response(r.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
  const bootLink = document.querySelector('link[data-boot]');
  const fromManifest = () => manifestP.then((m) => {
    const b = m.packs.boot;
    return b.gz ? fetchProgress('data/' + b.gz.url, onPak).then(inflate)
      .catch(() => fetchProgress('data/' + b.url, onPak).then((r) => r.arrayBuffer()))
      : fetchProgress('data/' + b.url, onPak).then((r) => r.arrayBuffer());
  });
  const pakP = (bootLink ? fetchProgress(bootLink.getAttribute('href'), onPak).then(inflate) : Promise.reject())
    .catch(fromManifest);
  wasmRespP.catch(() => {}); pakP.catch(() => {}); manifestP.catch(() => {});

  const canvas = $('canvas');
  const sizeCanvas = () => {
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.floor(canvas.clientWidth * dpr));
    canvas.height = Math.max(1, Math.floor(canvas.clientHeight * dpr));
  };
  sizeCanvas();
  // loader.js is the single owner of the canvas size (CSS 100vw/100vh, backing = CSS * dpr);
  // the wasm side only reads canvas.width/height each frame (CApplication::SyncCanvasSize).
  window.addEventListener('resize', sizeCanvas);
  if (window.ResizeObserver) new ResizeObserver(sizeCanvas).observe(canvas);

  const mod = {
    canvas,
    noInitialRun: true,
    dataBaseUrl: 'data/',
    print: (...a) => console.log(...a),
    printErr: (...a) => console.warn(...a),
    onSaveMounted: () => account.hooks.onSaveMounted(mod),
    onSaveFlushed: () => { account.hooks.onSaveFlushed(); },
    instantiateWasm(imports, done) {
      wasmRespP.then((r) => WebAssembly.instantiateStreaming(r, imports))
        .then((res) => done(res.instance, res.module))
        .catch(fail);
      return {};
    },
  };

  const createColobot = (await factoryP).default;
  mod.bootPak = new Uint8Array(await pakP);
  // WP11 contract: archiver reads Module.colobotFS before main().
  mod.colobotFS = { baseUrl: 'data/', manifest: await manifestP, packs: { boot: mod.bootPak } };
  const Module = await createColobot(mod);
  Module.colobotFS = mod.colobotFS;
  Module.bootPak = mod.bootPak;
  Module.dataBaseUrl = 'data/';
  window.Module = Module;
  setStatus('Starting…'); setProgress(1);

  // Audio unlock on first gesture.
  const unlock = () => {
    try {
      const ctxs = Module.AL && Module.AL.contexts;
      if (ctxs) for (const k in ctxs) {
        const c = ctxs[k] && ctxs[k].audioCtx;
        if (c && c.state !== 'running') c.resume();
      }
    } catch (e) { /* ignore */ }
  };
  for (const ev of ['pointerdown', 'keydown', 'touchend']) window.addEventListener(ev, unlock, { passive: true });

  // main() performs init (JSPI-suspending); frames are driven from JS.
  await Module.callMain([]);
  $('loading').style.display = 'none';
  canvas.focus();

  let busy = false, stopped = false;
  const tick = async () => {
    if (stopped) return;
    if (!busy && !document.hidden) {
      busy = true;
      try {
        const f0 = performance.now();
        const r = await Module._colobot_frame();
        const dt = performance.now() - f0;
        window.__colobotFrames = (window.__colobotFrames || 0) + 1;
        if (dt > 50) (window.__colobotLongFrames = window.__colobotLongFrames || []).push([Math.round(f0), Math.round(dt)]);
        if (r === 0) { stopped = true; return; }
      } catch (e) { stopped = true; fail(e); return; }
      finally { busy = false; }
    }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);

  // Prefetch likely next data at low priority once the menu is idle: the packs every level
  // needs (models, common). Music is fetched in the background by CALSound (web_fs_ready).
  const prefetch = () => {
    const c = Module.colobotFS;
    if (!c || !c.prefetch || navigator.connection && navigator.connection.saveData) return;
    for (const k of ['pack:models', 'pack:common']) if (c.manifest.packs[k.slice(5)]) c.prefetch(k, true);
  };
  setTimeout(() => (window.requestIdleCallback || setTimeout)(prefetch, { timeout: 5000 }), 3000);
}

main().catch(fail);
