// Cache-first for content-addressed data (keyed by manifest sha256), network-first for the rest.
const CACHE = 'colobot-v1';
const DATA = 'colobot-data';
let hashes = null; // pathname -> sha256 from data/manifest.json (packs + loose files)

async function loadHashes() {
  if (hashes) return hashes;
  try {
    const m = await (await fetch('data/manifest.json', { cache: 'no-cache' })).json();
    hashes = {};
    for (const p of Object.values(m.packs || {}))
      hashes[new URL('data/' + p.url, self.location).pathname] = p.sha256;
    for (const f of Object.values(m.files || {}))
      if (!f.pack && f.url) hashes[new URL('data/' + f.url, self.location).pathname] = f.sha256;
  } catch (e) { hashes = {}; }
  return hashes;
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
  if (url.pathname.includes('/data/') && !url.pathname.endsWith('manifest.json')) {
    e.respondWith((async () => {
      const h = (await loadHashes())[url.pathname];
      if (!h) return fetch(req);
      const key = url.origin + url.pathname + '?h=' + h;
      const cache = await caches.open(DATA);
      const hit = await cache.match(key);
      if (hit) return hit;
      const resp = await fetch(req);
      if (resp.ok) {
        cache.put(key, resp.clone());
        for (const k of await cache.keys())
          if (k.url.startsWith(url.origin + url.pathname + '?h=') && k.url !== key) cache.delete(k);
      }
      return resp;
    })());
    return;
  }
  // html / js / wasm / manifest: network-first, cache fallback offline
  e.respondWith((async () => {
    try {
      const resp = await fetch(req);
      if (resp.ok) (await caches.open(CACHE)).put(req, resp.clone());
      return resp;
    } catch (err) {
      const hit = await caches.match(req);
      if (hit) return hit;
      throw err;
    }
  })());
});
