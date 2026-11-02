# Response to review round 1 (fable-1, gemini-1)

Every finding was checked against the code before acting. Verification after the fixes: wasm and
data rebuilt, `scripts/assemble-dist.sh build-web .data-out`, `tests/e2e/boot.mjs` (menu) and
`--query runscene=missions101` render as before (menu, mission 101 with terrain, shadows, HUD), and the new
`tests/e2e/save-roundtrip.mjs` (create player, reload in the same profile, compare `/save`) passes.

## Fable

| # | Sev | Finding | Status |
|---|-----|---------|--------|
| 1 | blocker | Terrain/shadow bind-group cache keyed on raw `WGPUBuffer` | **Fixed** `dade21b`: key is a held `wgpu::Buffer`, so the address cannot be freed and reused; rule added to `docs/webgpu-renderer-guide.md`. UI/object/particle renderers already compared against a held buffer. |
| 2 | major | `/save` writes outside config/scene save never flushed to IDB | **Fixed** `0c9c602`: `COutputStreamBuffer::close`, `CResourceManager::CreateNewDirectory/RemoveExistingDirectory/Remove` call `web_save_dirty()` (covers SaveFinishedLevels, SaveFreeGameUnlock, SaveAppearance, player create/delete, savegame delete, CBot programs, screenshots). 250 ms debounce; `FS.syncfs` is serialized (one in flight, later requests coalesce); `pagehide`/`visibilitychange` and explicit `web_save_sync()` use the same queue. Tested with `9a24090`. |
| 3 | major | `_headers` joins `no-cache` with `immutable`; pack names not content-addressed | **Fixed** `718942f`: `build.mjs` names packs/loose files `name.<sha256[:12]>.ext`; `assemble-dist.sh` writes the current boot pack into the `<link data-boot>` preload; loader falls back to the manifest URL. `_headers` uses `! Cache-Control` before re-adding the value under `/data/*` and `/data/manifest.json`. After the next deploy, check with `curl -sI .../data/manifest.json` and a pack URL. |
| 4 | minor | `Destroy()` of resources that a recorded pass may still use | **Partly fixed** `4bd3249`: the replaced static vertex buffer is now released, not destroyed. Texture `Destroy`/`DestroyAll` and screen depth stay: they run outside `Render()` today, and destroying them right away frees VRAM on level change. A deferred destroy list is noted as a follow-up. |
| 5 | minor | Frames that suspend through JSPI present an unwritten swapchain image | **Deferred**: the fix needs an offscreen color target plus a copy on every frame. Suspending frames happen only during loads, and no black flash shows in the e2e captures. |
| 6 | minor | Savegame thumbnails are black | **Deferred** (known WP12 TODO): reading back pixels needs `MapAsync` and a JSPI wait, and that is separate work. |
| 7 | minor | Shadow depth bias units | **Deferred**: needs visual tuning. Mission 101 shadows show no acne at the e2e viewpoint. |
| 8 | minor | `/save/log.txt` never written on web | **Fixed** `4bd3249`: no log file on web. The console is the log, and a file under the IDBFS `/save` would be re-synced on every flush. |

## Gemini

| # | Sev | Finding | Status |
|---|-----|---------|--------|
| 1 | blocker | Uninitialized `RenderPassDescriptor` / wild `depthStencilAttachment` | **Rejected**: the `wgpu::` C++ structs from emdawnwebgpu `webgpu_cpp.h` have default member initializers (`depthStencilAttachment = nullptr`, `stencilLoadOp = Undefined`, …), so `wgpu::RenderPassDescriptor desc;` is fully initialized. Color-only passes run every frame in the e2e runs with no validation errors. |
| 2 | blocker | `_malloc` null in `wfs_js_fetch` | **Fixed** `0c9c602` (cheap hardening; `wfs_js_boot` too). The quoted code (`setValue`, `out_len`) does not match the source. |
| 3 | blocker | IDBFS sync missing on most write paths | **Fixed**, same as Fable #2. |
| 4 | major | `Promise.race` in the SW skips the cache on cold start | **Fixed** by redesign `718942f`: data URLs are immutable, so the SW is cache-first by pathname and no longer needs a hash table. |
| 5 | major | `colobot-data` cache never pruned; stale `hashesP` | **Fixed** `718942f`: every fresh `manifest.json` that passes through the SW prunes data entries it no longer references. Cache names were bumped (`colobot-v3`, `colobot-data-v2`), and `activate` deletes the old caches. |
| 6 | major | Query string becomes engine argv (injection) | **Rejected**: this is intended (`?runscene=` drives e2e). It runs client-side only, in the visitor's own origin sandbox: `-savedir`/`-datadir`/`-mod` can only point at that tab's own MEMFS/IDBFS, with no server, privilege boundary or other user involved. A crafted link can at most change the visitor's own session, just as editing the URL would. |
| 7 | major | JSPI re-entrancy from DOM events or rAF while suspended | **Rejected**: `loader.js` `tick` holds `busy` until `_colobot_frame()` resolves, so rAF never re-enters. No SDL event filter/watcher is installed, so SDL only queues DOM events, and the next frame polls them (Fable confirmed this independently). |
| 8 | major | Resize feedback loop through `SDL_SetWindowSize` | **Rejected, already fixed** in `9b47905`: `loader.js` owns the canvas size, and `SyncCanvasSize` only mirrors `canvas.width/height` (pixel ratio 1, no CSS write that changes the observed box). The e2e runs show a stable size. |
| 9 | minor | `mio_duplicate` resets the position | **Rejected**: the PhysFS `duplicate` contract asks for an independent copy, and PhysFS's own memory io (`memoryIo_duplicate`, `physfs.c`) also starts the copy at `pos = 0`. Callers seek before reading. |
| 10 | minor | `PHYSFS_Io` leak in `wa_openArchive` | **Fixed** `0c9c602`: once the archiver claims the io it owns it (see `openDirectory` in `physfs.c`), so the io is now destroyed right away. It was a one-off leak of a few bytes. |
| 11 | minor | Hard-coded 256 dynamic-offset alignment | **Rejected**: the WebGPU spec caps `minUniformBufferOffsetAlignment` at 256 (that is the default, and adapters may only report smaller values), so 256 is always valid. |
