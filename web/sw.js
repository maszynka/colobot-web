// Data URLs are content-addressed (name.<sha256[:12]>.ext, tools/build-data/build.mjs), so
// /data/* is cache-first forever; everything else (html/js/wasm/manifest) is network-first.
// Whenever a fresh manifest.json passes through, data entries it no longer references are pruned.
const CACHE = 'colobot-v3';
const DATA = 'colobot-data-v2';

async function pruneData(manifestResp) {
  try {
    const m = await manifestResp.json();
    const keep = new Set();
    const add = (u) => keep.add(new URL('data/' + u, self.location).pathname);
    for (const p of Object.values(m.packs || {})) { add(p.url); if (p.gz) add(p.gz.url); }
    for (const f of Object.values(m.files || {})) if (!f.pack && f.url) add(f.url);
    const cache = await caches.open(DATA);
    for (const k of await cache.keys())
      if (!keep.has(new URL(k.url).pathname)) await cache.delete(k);
  } catch (e) { /* offline or malformed: keep what we have */ }
}

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (e) => e.waitUntil((async () => {
  await clients.claim();
  for (const k of await caches.keys()) if (k !== CACHE && k !== DATA) await caches.delete(k);
})()));

self.addEventListener('fetch', (e) => {
  const req = e.request;
  if (req.method !== 'GET' || req.headers.has('range')) return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;
  if (url.pathname.startsWith('/api/')) return; // accounts + saves: never cached
  const isManifest = url.pathname.endsWith('/data/manifest.json');
  if (url.pathname.includes('/data/') && !isManifest) {
    e.respondWith((async () => {
      const cache = await caches.open(DATA);
      const hit = await cache.match(url.pathname);
      if (hit) return hit;
      const resp = await fetch(req);
      if (resp.ok && resp.status === 200) e.waitUntil(cache.put(url.pathname, resp.clone()));
      return resp;
    })());
    return;
  }
  // html / js / wasm / manifest: network-first, cache fallback offline
  e.respondWith((async () => {
    try {
      const resp = await fetch(req, isManifest ? { cache: 'no-cache' } : undefined);
      if (resp.ok) {
        e.waitUntil((await caches.open(CACHE)).put(req, resp.clone()));
        if (isManifest) e.waitUntil(pruneData(resp.clone()));
      }
      return resp;
    } catch (err) {
      const hit = await caches.match(req);
      if (hit) return hit;
      throw err;
    }
  })());
});
