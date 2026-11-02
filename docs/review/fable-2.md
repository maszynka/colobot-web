# Review round 2: colobot-web fixes (fable-2)

Scope: `git diff dade21b~1 HEAD -- . ':!third_party'` (commits `dade21b`, `0c9c602`, `718942f`, `9a24090`,
`4bd3249`), checked against the surrounding source, `dist/` output, Cloudflare `_headers` docs and the
workers-sdk implementation (`packages/workers-shared/utils/configuration/parseHeaders.ts`,
`constants.ts`, `asset-worker/src/utils/headers.ts`). Every statement below was checked against code.

## Round-1 fixes: verified

| # | Finding | Verification |
|---|---------|--------------|
| F1 | Terrain/shadow bind-group key on raw `WGPUBuffer` | Correct and complete. `m_uniformGroupBuffer` is now a held `wgpu::Buffer` (`wgpu_terrain_renderer.h:109`, `wgpu_shadow_renderer.h:88`); the held ref keeps the old chunk alive after `CWebGPURingBuffer::BeginFrame()` merges, so its address cannot be reused and `.Get() != a.buffer.Get()` fires on growth. Re-checked the remaining raw-handle caches: object/particle/UI renderers (`m_uniformGroupBuffer` compared against held `m_uniformBuffer.Get()`) and `CWebGPUSceneBindings` (`m_bgBuffer` vs held `m_allocBuffer.Get()`, `wgpu_scene.cpp:100-120`) are all safe for the same reason. `wgpu_pass_manager.cpp:45` compares held `TextureView`s. No other raw-handle keys exist. |
| F2 | `/save` writes not flushed to IDB | Correct and complete. All PhysFS write paths in `src/` go through `COutputStreamBuffer` (`LoudOpenWrite/Append` have no other callers), `CResourceManager::CreateNewDirectory/RemoveExistingDirectory/Remove`; each now calls `web_save_dirty()`. Savegame screenshots also go through `COutputStream` (`image.cpp:97` `PNGSaveSurface`), so they are covered too. The JS queue (`wfs_js_install_sync`) is correct: one `FS.syncfs` in flight, later callers coalesce into one follow-up whose promise resolves after a sync that started after the call; `__wfsFlushNow` cancels the debounce timer and `wfs_js_sync`/`pagehide`/`visibilitychange` all use it. `web_save_dirty()` before mount is a guarded no-op. Side benefit: `m_file = nullptr` in `close()` removes a pre-existing double `PHYSFS_close` (destructor called `close()` again after an explicit `close()`). `tests/e2e/save-roundtrip.mjs` exercises create -> reload. |
| F3 | `_headers` comma-join, non-addressed data URLs | Correct. `build.mjs` `hashed()` yields `boot.<sha12>.pak`, `boot.<sha12>.pak.gz`, `music/x.<sha12>.ogg`; `.data-out/` and `dist/data/` confirm. `assemble-dist.sh` regex `href="[^"]*"([^>]*data-boot)` cannot cross `>` so it rewrites only the `data-boot` link (verified in `dist/index.html:12`). `_headers`: workers-sdk applies, per matching rule in file order, `unset` (`headers.delete`) first then `set`; `UNSET_OPERATOR = "! "` matches the file's `! Cache-Control`. `setMap` is not cleared on delete, so the re-add goes through `append`, but append on an absent header equals set, so `/data/x` ends with exactly `public, max-age=31536000, immutable` and `/data/manifest.json` with `no-cache`. `/*.wasm` and `/sw.js` still get `no-cache` from `/*`. Confirm after deploy with `curl -sI`. |
| F4 | `Destroy()` mid-frame | Partial fix (vertex buffer release) is correct; texture/depth `Destroy` remain outside `Render()`. Acceptable, follow-up noted by author. |
| F8 | `/save/log.txt` | Correct; no file on web. |
| G2 | `_malloc` null | Correct in `wfs_js_fetch` and `wfs_js_boot`. |
| G4/G5 | SW race / pruning | Correct. Cache-first by pathname is sound now that names are content-addressed. `e.waitUntil` is called before `return resp` while the `respondWith` promise is pending, so it is valid after the `await`s. `pruneData` keys (`new URL('data/'+u, self.location).pathname`) match `cache.put(url.pathname)` keys. Cache names bumped and old ones deleted on `activate`. |
| G10 | `PHYSFS_Io` leak | Correct: archiver owns `io` on claim and destroys it. |

## Rejections assessed

Gemini #1 (uninitialized `wgpu::` descriptors): rejection correct, emdawnwebgpu `webgpu_cpp.h` structs have default
member initializers. #6 (query -> argv): acceptable; client-side only, no privilege boundary. #7 (JSPI
re-entrancy): correct, `tick` holds `busy` and no SDL event filters are installed. #9 (`mio_duplicate` pos):
correct, PhysFS's own `memoryIo_duplicate` starts at 0. #11 (256 alignment): correct, spec maximum is 256.
#8 (resize loop): consistent with `loader.js` owning the canvas size; not re-derived in depth.
Deferred Fable #5/#6/#7: reasonable, none affects saves or deploy.

## New findings (none blocking)

### 1. [minor] `assemble-dist.sh` without a data dir leaves a stale boot preload
**File:** `scripts/assemble-dist.sh:11`, `web/index.html:12`.
**Scenario:** `assemble-dist.sh build-web` (no `DATA`) ships `href="data/boot.pak.gz"`, which no longer exists.
`wrangler.jsonc` has `not_found_handling: "none"`, so the fetch 404s, `fetchProgress` throws and
`loader.js:65` falls back to the manifest URL: the game still boots, at the cost of one wasted request and a
console warning. **Fix:** fail the script when `dist/data/manifest.json` is missing, or strip the `data-boot`
link in that case.

### 2. [minor] Loader always gunzips the preloaded pack
**File:** `web/loader.js:65`, `scripts/assemble-dist.sh:14`.
**Scenario:** if a manifest ever lacks `packs.boot.gz`, `assemble-dist.sh` writes the raw `.pak` URL into the
preload and the loader pipes it through `DecompressionStream('gzip')`, fails, and downloads the pack a second
time via `fromManifest`. Not reachable with the current `build.mjs` (always emits `.gz`). **Fix:** key on the
URL ending in `.gz`, or add `data-gz` to the link.

### 3. [minor] Deploy during a live session now 404s never-fetched packs
**Files:** `web/sw.js:33-39`, `src/colobot-base/src/common/resources/web_archiver.c` (`c.get`).
**Scenario:** a tab opened before a redeploy holds the old manifest; a level pack it has not fetched yet is
requested under the old hash, the SW has no cached copy and Workers assets returns 404, so
`wfs_js_fetch` returns -1 and the level fails to load. Before this change the same tab got the new pack with
old offsets (corruption), so this is an improvement, not a regression; listed so the behaviour is known.
**Fix (optional):** on a `/data/*` 404, have `loader.js`/the archiver reload the page or show a "new version
available" prompt.

### 4. [minor] `hashed()` edge for dotfiles
**File:** `tools/build-data/build.mjs:104-109`. A basename starting with `.` yields `.<sha>.name`. No such
files are selected by `INCLUDE`/`EXCL_*` today; harmless.

### 5. [docs] `docs/STATUS.md:65` still describes the old SW (sha256 keys, revalidation, `Promise.race`).

## Verified no regression
- `wgpu_device.cpp:71` `m_buffer = nullptr`: the recorded pass keeps the old `GPUBuffer` alive on the JS side; no `Destroy`.
- `COutputStreamBuffer::close()` idempotent; no caller checks `is_open()` after `close()`.
- `MarkSaveDirty()` in `CreateNewDirectory` is reached by `VerifyPaths()` before `web_fs_mount_save()` installs the queue: guarded (`if (Module.__wfsDirty)`), and afterwards it only schedules a cheap flush.
- `tests/e2e/boot.mjs` has no hard-coded pack names; `.data-out` and `dist/data` contain only hashed names plus `manifest.json`.
- `sw.js` `isManifest` test (`endsWith('/data/manifest.json')`) keeps the manifest on the network-first path with `cache: 'no-cache'`; the loader fetches it the same way.

VERDICT: OK
