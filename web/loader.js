// Colobot web loader. Contract with the wasm side:
//   Module.bootPak      Uint8Array of data/boot.pak (WP11 reads it)
//   Module.dataBaseUrl  'data/' (manifest.json and loose files/packs live there)
//   Module.canvas       <canvas id="canvas">
//   Exports used: _colobot_frame() -> int (0 = quit), callMain([])
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

async function fetchProgress(url, onBytes, wasm) {
  const resp = await fetch(url);
  if (!resp.ok) throw new Error(url + ': HTTP ' + resp.status);
  const total = +resp.headers.get('content-length') || 0;
  let got = 0;
  const reader = resp.body.getReader();
  const stream = new ReadableStream({
    async pull(ctl) {
      const { done, value } = await reader.read();
      if (done) { ctl.close(); return; }
      got += value.length; onBytes(got, total); ctl.enqueue(value);
    },
    cancel(r) { return reader.cancel(r); },
  });
  const headers = new Headers(resp.headers);
  if (wasm) headers.set('content-type', 'application/wasm');
  return new Response(stream, { status: 200, headers });
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

  const progress = { wasm: [0, 1], pak: [0, 1] };
  const update = () => {
    const got = progress.wasm[0] + progress.pak[0];
    const tot = progress.wasm[1] + progress.pak[1];
    setProgress(got / tot);
    setStatus('Downloading… ' + (got / 1048576).toFixed(1) + ' MB');
  };

  // Start everything in parallel.
  const factoryP = import('./colobot.js');
  const wasmRespP = fetchProgress('colobot.wasm', (g, t) => { progress.wasm = [g, t || g + 1]; update(); }, true);
  const pakP = fetchProgress('data/boot.pak', (g, t) => { progress.pak = [g, t || g + 1]; update(); })
    .then((r) => r.arrayBuffer());
  const manifestP = fetch('data/manifest.json').then((r) => r.json());
  wasmRespP.catch(() => {}); pakP.catch(() => {}); manifestP.catch(() => {});

  const canvas = $('canvas');
  const sizeCanvas = () => {
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.floor(canvas.clientWidth * dpr));
    canvas.height = Math.max(1, Math.floor(canvas.clientHeight * dpr));
  };
  sizeCanvas();
  window.addEventListener('resize', sizeCanvas);

  const mod = {
    canvas,
    noInitialRun: true,
    dataBaseUrl: 'data/',
    print: (...a) => console.log(...a),
    printErr: (...a) => console.warn(...a),
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
        const r = await Module._colobot_frame();
        if (r === 0) { stopped = true; return; }
      } catch (e) { stopped = true; fail(e); return; }
      finally { busy = false; }
    }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

main().catch(fail);
