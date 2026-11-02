# Review: colobot-web (Emscripten 6 + emdawnwebgpu + JSPI) — fable-1

Scope: `git diff a09cf54 HEAD -- . ':!third_party' ':!docs' ':!package-lock.json'` plus surrounding source,
generated glue (`build-web/src/colobot.js`), the emdawnwebgpu port sources in the Emscripten cache
(`/opt/homebrew/Cellar/emscripten/6.0.13/libexec/cache/ports/emdawnwebgpu/.../webgpu.cpp`) and the
Cloudflare `_headers` documentation. Every finding below was checked against code.

Checked and found correct (no finding): all C++/WGSL uniform struct layouts (LightUniforms 64,
ShadowUniforms 336, TerrainUniforms 320, ObjectUniforms 384, UI/Particle 80, Shadow 64 — field order
and vec3/f32 packing match WGSL rules); dynamic offsets 256-aligned; `fix_clip` z remap; shadow-map V
flip; FrontFace::CW with z-flipped view; bgra8/rgba8 swapchain handling (offscreen color FBs follow
`SurfaceFormat()`, uploads stay RGBA8 and `CopyFramebufferToTexture` refuses mismatched formats);
`callMain` in Emscripten 6 does `ret = await ret` so `await Module.callMain([])` really waits for
`main()`; the only `EM_ASYNC_JS` functions (`wfs_js_fetch`, `wfs_js_mount_save`, `wfs_js_sync`) are
reached only from `main()`/`colobot_frame()` (both in `-sJSPI_EXPORTS`); no SDL event filters/watchers
are installed, so DOM-event callbacks into wasm never reach PhysFS; `dist/` and the wasm string table
contain no `/Users/`, `/opt/homebrew` or user names.

---

## 1. [blocker] Stale uniform bind group after ring-buffer growth (terrain + shadow renderers)

**Files:** `src/colobot-base/src/graphics/webgpu/wgpu_terrain_renderer.cpp:272-285`,
`src/colobot-base/src/graphics/webgpu/wgpu_shadow_renderer.cpp:224-237`,
`src/colobot-base/src/graphics/webgpu/wgpu_ring_buffer.cpp:46-59`.

Both renderers cache their group(1) bind group keyed only on the raw handle:

```cpp
if (!m_uniformGroup || m_uniformGroupBuffer != a.buffer.Get()) { ... rebuild ... }
```

and store `WGPUBuffer m_uniformGroupBuffer` without holding a `wgpu::Buffer` reference (the UI, object
and particle renderers and `CWebGPUSceneBindings` keep `m_uniformBuffer`/`m_allocBuffer`, so they are
safe). In emdawnwebgpu a `WGPUBuffer` is a plain heap pointer; `wgpuBufferRelease` does
`delete o; emwgpuDelete(o);` (webgpu.cpp:1795-1798). `CWebGPURingBuffer::BeginFrame()` after an
overflow frame does `m_chunks.clear(); AddChunk(size);` — the old `Buffer` objects are freed and the
new one is `new`ed immediately with the same class size, so the allocator hands back the address of the
most recently freed chunk (LIFO), which is exactly the chunk the terrain/shadow renderer last allocated
from.

**Failure scenario:** any frame whose uniform traffic exceeds the current ring chunk (1 MiB initial →
~2000 object draws, or fewer when UI draws at 256 B each are added). Next `BeginScene()` merges chunks;
the terrain (and shadow) renderer sees `m_uniformGroupBuffer == a.buffer.Get()` and keeps the bind
group that references the *released* GPUBuffer (kept alive on the JS side only by the GPUBindGroup).
That buffer is never written again, so terrain is drawn with frozen matrices/fog and shadow-map
draws use a frozen MVP for the rest of the session (until the next growth). No validation error is
raised, so it is silent.

**Fix:** hold the buffer (`wgpu::Buffer m_uniformBuffer = a.buffer;`) alongside the raw handle like
the other renderers, or key the cache on a ring "generation" counter incremented in `AddChunk()` /
`BeginFrame()` merge (expose it from `CWebGPURingBuffer`) instead of the pointer. Apply the same rule
in `docs/webgpu-renderer-guide.md`.

## 2. [major] Save-data writes are not flushed to IndexedDB (progress loss on tab close)

**Files:** `src/colobot-base/src/common/resources/web_archiver.c:139-153`,
`src/colobot-base/src/common/config_file.cpp:130-132`, `src/colobot-base/src/level/robotmain.cpp:4674`,
`src/colobot-base/src/level/player_profile.cpp:157,164,301,385,438,517,580`.

`web_save_sync()` (IDBFS `syncfs(false)`) is called only from `CConfigFile::Save()` and
`CRobotMain::IOWriteScene()`. Everything else that writes under `/save` stays in MEMFS only:
`CPlayerProfile::SaveFinishedLevels` (level completion/unlocks, `*.gam`), `SaveFreeGameUnlock`,
`SaveAppearance`, player profile create/delete (`CreateNewDirectory`/`RemoveExistingDirectory`), and
savegame deletion. The only fallback is the `pagehide`/`visibilitychange` handler, which fires an
asynchronous `FS.syncfs(false)` with no guarantee the IndexedDB transaction commits before the
document is torn down (Chrome usually does, Safari/mobile frequently do not).

**Failure scenario:** finish a mission, close the tab → the mission is not marked as completed on
the next visit; delete a save → it reappears; create a player, close → player gone. Conversely a save
deleted in-session reappears after reload if a sync never ran.

**Fix:** either call `web_save_sync()` from `CPlayerProfile::SaveFinishedLevels/SaveFreeGameUnlock/
SaveAppearance`, `CPlayerProfile::Create/Delete`, and the savegame-delete path, or (simpler and more
robust) make the sync debounced-and-automatic: mark dirty in a PhysFS write hook/`COutputStream` close
and flush once per N frames from `CApplication::Frame()` when dirty. Keep `pagehide` as a best effort
only. Also serialize concurrent `syncfs` calls (the pagehide flush can overlap a JSPI-suspended
`wfs_js_sync`; Emscripten logs a warning and may skip).

## 3. [major] `/data/*` caching: `_headers` does not do what it says, and the SW relies on HTTP-cache semantics it does not control

**Files:** `web/_headers:1-16`, `web/sw.js:33-55`, `tools/build-data/build.mjs` (file naming).

Cloudflare Workers static assets docs: "If a header is applied twice in the `_headers` file, the
values are joined with a comma separator." `/*` sets `Cache-Control: no-cache` and `/data/*` adds
`public, max-age=31536000, immutable`, so data responses actually carry
`Cache-Control: no-cache, public, max-age=31536000, immutable`. Browsers treat `no-cache` as
"always revalidate", so (a) the intended immutable caching of packs is not in effect (every pack is a
conditional GET on each cold load), and (b) the comment in `sw.js` ("Data names are not
content-addressed: revalidate so a stale HTTP-cache copy never gets stored") describes a hazard that
is only being avoided by this accident.

Latent bug if the header is "fixed" (e.g. `! Cache-Control` under `/data/*`): on every page load the
first `/data/` request (`boot.pak.gz`, kicked off by the `<link rel=preload>` in `index.html`) hits the
SW while `hashesP` is still unresolved (`Promise.race([pending, Promise.resolve(null)])` returns `null`
on a cold SW), so it goes `fetch(req)` → browser HTTP cache → a year-old `boot.pak.gz`, while
`manifest.json` (no-cache) is new. `wfs_js_fetch` then slices the *old* boot pack with the *new*
manifest offsets → corrupt fonts/textures, menu unusable after each redeploy. Also the local e2e server
sends `Cache-Control: no-store`, so none of this is exercised by `tests/e2e/boot.mjs`.

**Fix (pick one, first preferred):** make pack/loose URLs content-addressed in `build.mjs`
(`boot.<sha256-prefix>.pak.gz`, manifest maps logical name → url) and then `immutable` is correct and
the SW hash table becomes unnecessary for correctness; or keep names and always
`await loadHashes()` for `/data/` requests (manifest is tiny and `no-cache`) and use
`fetch(req, {cache: 'no-cache'})` in the not-in-table path too; and in either case remove the
duplicate `Cache-Control` (use `! Cache-Control` before re-adding under `/data/*`) so the served value
is intentional. Verify with `curl -sI https://<host>/data/boot.pak.gz | grep -i cache-control` after
deploy.

## 4. [minor] GPU resources may be destroyed while referenced by the open frame encoder

**Files:** `wgpu_device.cpp:63` (`m_buffer.Destroy()` in `CWebGPUVertexBuffer::Update`),
`wgpu_texture.cpp:239,251` (`texture.Destroy()` in `Destroy/DestroyAll`),
`wgpu_device.cpp:239-253` (`CreateScreenDepth` destroys the depth texture in `ConfigureSurface`).

`Destroy()` (not just Release) is immediate. If any of these run between `BeginScene()` and
`EndScene()` after the resource was used by a recorded pass, `queue.Submit` fails validation and the
whole frame is dropped. Today the engine updates geometry/textures and reacts to resize outside
`Render()`, so this does not trigger, but it is a trap for future callers (e.g. a `ChangeVideoConfig`
from an event handled mid-render). **Fix:** drop the explicit `Destroy()` calls (release is enough; the
JS GC frees the GPU object) or defer them to `EndScene()` via a per-frame "to destroy" list.

## 5. [minor] Frames that suspend (JSPI) present an unfinished swapchain image

**Files:** `web/loader.js:122-137`, `wgpu_device.cpp:273-300`.

`BeginScene()` acquires the current texture; if `Frame()` later suspends (any PhysFS read → `wfs_js_fetch`,
`web_save_sync`, music decode path), the rAF task ends before `EndScene()` submits, so the browser
presents the acquired-but-unwritten image (black flash) for that frame, and the game's own loading
screen is drawn only on the next task. **Fix:** skip presenting on suspended frames by rendering to an
offscreen color texture and copying to the swapchain only in `EndScene()` (one `CopyTextureToTexture`,
formats already match), or accept and document.

## 6. [minor] Savegame thumbnails are black

**File:** `wgpu_device.cpp:71-92,472-477` (`GetFrameBufferPixels` stub).
`CRobotMain::IOWriteScene` → `WriteScreenShot` writes an opaque black PNG for every save. Known
TODO(WP12); listed because it is user-visible in the load dialog. Readback via
`CopyTextureToBuffer` + `MapAsync` + `WaitAny` (JSPI) is straightforward with the current design.

## 7. [minor] Shadow depth bias units differ from GL

**File:** `wgpu_shadow_renderer.cpp:243-244` (`depthBias = 8`, slope 2 for Depth32Float).
GL `glPolygonOffset(2, 8)` on a 24-bit depth buffer ≈ 8 × 2⁻²⁴; on Depth32Float WebGPU's constant unit
is 2^(e−23) of the current depth, i.e. much smaller near 1.0 → possible shadow acne at distance. Tune
visually (e.g. 8→ 2–4 with slope 2) rather than copying GL constants.

## 8. [minor] `/save/log.txt` is never written on web

**File:** `src/colobot-app/src/main.cpp:215`. The ofstream is opened before `VerifyPaths()` creates
`/save` (MEMFS dir does not exist yet), so the log file always fails; harmless, console logging works.
Move `web_fs_mount_save()` earlier or open the log after `VerifyPaths()`.

---

Not findings but worth noting: `-sEXIT_RUNTIME=0` with `callMain` throwing `ExitStatus` through
`handleException` is the intended path and works; `wa_open` claims any archive named `*manifest.json`
(fine, only one mount); `web_archiver` JS cache limit (128 MiB) plus one transient wasm copy per open
file is bounded; `ArgsFromUrlQuery` lets `?savedir=`/`?datadir=` override paths client-side only.

VERDICT: BLOCKING
