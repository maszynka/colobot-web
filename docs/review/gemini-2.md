# Round 2 Code Review: Colobot Browser Port (Emscripten + WebGPU + JSPI)

This re-review evaluates the changes introduced in `.review/diff2.patch` against the round-1 reviews ([`gemini-1.md`](file://<repo>/docs/review/gemini-1.md), [`fable-1.md`](file://<repo>/docs/review/fable-1.md)) and the author's responses in [`response-1.md`](file://<repo>/docs/review/response-1.md).

---

## 1. Verification of Fixes

### 1.1 WebGPU Uniform Bind Group Buffer References (Fable #1)
- **Files**: [`wgpu_shadow_renderer.h:88`](file://<repo>/src/colobot-base/src/graphics/webgpu/wgpu_shadow_renderer.h#L88), [`wgpu_shadow_renderer.cpp:244`](file://<repo>/src/colobot-base/src/graphics/webgpu/wgpu_shadow_renderer.cpp#L244), [`wgpu_terrain_renderer.h:109`](file://<repo>/src/colobot-base/src/graphics/webgpu/wgpu_terrain_renderer.h#L109), [`wgpu_terrain_renderer.cpp:323`](file://<repo>/src/colobot-base/src/graphics/webgpu/wgpu_terrain_renderer.cpp#L323)
- **Status**: **Correct and Complete**.
- **Analysis**: `m_uniformGroupBuffer` has been converted from a raw pointer `WGPUBuffer` to a held RAII handle `wgpu::Buffer`. When uniform allocations cross into a new ring buffer chunk or `m_chunks.clear()` runs, the old buffer handle cannot be deallocated and immediately re-minted with the same heap address because `m_uniformGroupBuffer` retains an internal reference count. The cache check `m_uniformGroupBuffer.Get() != a.buffer.Get()` accurately detects new ring chunks, and `docs/webgpu-renderer-guide.md` has been updated with this requirement.

### 1.2 Save Data Writes & IDBFS Sync Queue (Fable #2 / Gemini #3)
- **Files**: [`web_archiver.c:140-175`](file://<repo>/src/colobot-base/src/common/resources/web_archiver.c#L140-L175), [`outputstreambuffer.cpp:64-73`](file://<repo>/src/colobot-base/src/common/resources/outputstreambuffer.cpp#L64-L73), [`resourcemanager.cpp:55-60,170-195,280-290`](file://<repo>/src/colobot-base/src/common/resources/resourcemanager.cpp#L55-L60)
- **Status**: **Correct and Complete**.
- **Analysis**:
  - `web_save_dirty()` was added to `COutputStreamBuffer::close()` (and thereby its destructor `~COutputStreamBuffer()`), as well as `CResourceManager::CreateNewDirectory`, `RemoveExistingDirectory`, and `Remove`. This covers CBot program saves, level unlocks (`*.gam`), player profile creation/deletion, and savegame management.
  - The JS synchronization queue in `wfs_js_install_sync()` coordinates all IDBFS writes:
    - Debounces background writes to 250ms (`Module.__wfsDirty()`).
    - Flushes immediately on explicit calls (`web_save_sync()`), `pagehide`, and `visibilitychange: hidden` (`Module.__wfsFlushNow()`).
    - Guarantees at most one `FS.syncfs(false)` is in flight at any time; requests arriving mid-sync are coalesced into a single sequential follow-up (`again` promise chain).
    - Verified by the end-to-end test [`tests/e2e/save-roundtrip.mjs`](file://<repo>/tests/e2e/save-roundtrip.mjs).

### 1.3 Content-Addressed Data URLs, `_headers`, and Service Worker (Fable #3 / Gemini #4, #5)
- **Files**: [`tools/build-data/build.mjs:882-930`](file://<repo>/tools/build-data/build.mjs#L882-L930), [`web/_headers:8-16`](file://<repo>/web/_headers#L8-L16), [`web/sw.js:1-58`](file://<repo>/web/sw.js#L1-L58), [`scripts/assemble-dist.sh:7-16`](file://<repo>/scripts/assemble-dist.sh#L7-L16), [`web/loader.js:63-74`](file://<repo>/web/loader.js#L63-L74)
- **Status**: **Correct and Complete**.
- **Analysis**:
  - `build.mjs` outputs data files with SHA256 hashes (`name.<sha256[:12]>.ext`).
  - `web/_headers` uses `! Cache-Control` before setting `Cache-Control: public, max-age=31536000, immutable` for `/data/*`, and `! Cache-Control` followed by `Cache-Control: no-cache` for `/data/manifest.json`. This complies with Cloudflare's header inheritance and negation syntax.
  - `scripts/assemble-dist.sh` injects the content-hashed `boot.<hash>.pak.gz` into `<link data-boot>` in `dist/index.html`.
  - `web/sw.js` simplifies caching: data requests are cache-first by pathname without requiring an in-flight hash lookup table (`Promise.race` hazard eliminated). On network fetch of a new `manifest.json`, `pruneData()` parses the active pack and file list and purges stale entries from `colobot-data-v2`.

### 1.4 Memory Allocation Safety (Gemini #2)
- **Files**: [`web_archiver.c:110-115,132-135`](file://<repo>/src/colobot-base/src/common/resources/web_archiver.c#L110-L115)
- **Status**: **Correct and Complete**.
- **Analysis**: `wfs_js_boot` and `wfs_js_fetch` now check `if (!ptr)` immediately after `_malloc`. In the event of allocation failure, an error message is logged and `-1` is returned. `wa_openRead` sets `PHYSFS_ERR_IO` and aborts cleanly without zero-page corruption.

### 1.5 PhysFS Archive Handle Ownership (Gemini #10)
- **Files**: [`web_archiver.c:256-261`](file://<repo>/src/colobot-base/src/common/resources/web_archiver.c#L256-L261)
- **Status**: **Correct and Complete**.
- **Analysis**: `wa_open()` calls `io->destroy(io)` upon claiming the archive, properly releasing the stream handle allocated by PhysFS.

### 1.6 Web Logging Configuration (Fable #8)
- **Files**: [`src/colobot-app/src/main.cpp:215-225`](file://<repo>/src/colobot-app/src/main.cpp#L215-L225)
- **Status**: **Correct and Complete**.
- **Analysis**: Wrapped `logFile` initialization with `#ifndef __EMSCRIPTEN__`. Browser builds direct output to the web console, avoiding redundant writes and sync cycles on `/save/log.txt`.

---

## 2. Evaluation of Author Rejections

All six rejections from round 1 were reviewed against upstream source code and specifications:

1. **Gemini #1 (Uninitialized `RenderPassDescriptor`)**: **Rejection has merit**.
   The C++ struct definitions in Dawn / `emdawnwebgpu` (`webgpu_cpp.h`) include default member initializers (e.g. `depthStencilAttachment = nullptr`). Instantiating `wgpu::RenderPassDescriptor desc;` is fully default-initialized.
2. **Gemini #6 (Command-line injection via URL query)**: **Rejection has merit**.
   Query parameters are parsed client-side and passed to the wasm engine running in the user's isolated origin sandbox without backend exposure or elevation of privilege. This is an intended mechanism for e2e automation (e.g., `?runscene=`).
3. **Gemini #7 (JSPI Async Re-entrancy)**: **Rejection has merit**.
   `loader.js` guards `tick()` with an atomic `busy` flag that remains `true` throughout JSPI stack suspensions until `_colobot_frame()` settles. Furthermore, SDL queues DOM events rather than synchronously invoking game loop callbacks.
4. **Gemini #8 (Canvas resize feedback loop)**: **Rejection has merit**.
   `loader.js` maintains sole ownership of CSS layout dimensions, and `SyncCanvasSize` in `app.cpp` only mirrors the backing pixel dimensions without triggering CSS layout thrashing.
5. **Gemini #9 (`mio_duplicate` reset)**: **Rejection has merit**.
   Conforms to the PhysFS internal memory IO specification (`memoryIo_duplicate`), where duplicated streams initialize `pos = 0` and callers seek as needed.
6. **Gemini #11 (Dynamic uniform buffer alignment query)**: **Rejection has merit**.
   The WebGPU specification defines the default `minUniformBufferOffsetAlignment` limit as 256 bytes, and adapters can only report smaller (more permissive) power-of-two alignments. Aligning to 256 bytes is spec-compliant across all WebGPU devices.

---

## 3. Investigation of Potential Regressions

| Subsystem | Area Checked | Assessment |
| :--- | :--- | :--- |
| **Save Sync Queue** | Overlapping flushes, `pagehide` handling, exception handling | `Module.__wfsFlush` correctly chains `running` and `again`. Promises always settle even on IDBFS error (`res()` invoked). Synchronous writes invoke non-suspending `wfs_js_dirty()`. No deadlock or unhandled rejections detected. |
| **Content Hashing & SW** | CDN caching, Cloudflare `_headers`, Service Worker cache/prune | `! Cache-Control` prevents header concatenation. Cache keys and prune sets resolve to identical absolute pathnames. Cold loads and asset redeployments prune cleanly without cache misses. |
| **WebGPU Bind Groups** | Ring buffer chunk transitions & memory lifetime | `wgpu::Buffer` refcounts prevent premature chunk deallocation and pointer reuse. Bind groups rebind properly when ring chunks grow. |

No blocking defects affecting gameplay, saves, or deployment were introduced.

---

VERDICT: OK
