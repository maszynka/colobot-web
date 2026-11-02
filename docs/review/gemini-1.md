# Code Review: Colobot Browser Port (Emscripten + WebGPU + JSPI)

This review evaluates the changes introduced in `.review/diff.patch` against pristine upstream, focusing on WebGPU correctness, JSPI/async suspension, memory management, filesystem/IDBFS persistence, asset caching/service worker lifecycle, security, and build scripts.

---

## Blocker Findings

### 1. WebGPU: Uninitialized `RenderPassDescriptor` fields and wild pointer in `CWebGPUPassManager::GetPass`
- **File:Line**: `src/colobot-base/src/graphics/webgpu/wgpu_pass_manager.cpp:52-78` (patch lines ~5666–5692)
- **Failure Scenario**: 
  `wgpu::RenderPassDescriptor desc;`, `wgpu::RenderPassColorAttachment colorAttachment;`, and `wgpu::RenderPassDepthStencilAttachment depthAttachment;` are allocated as uninitialized automatic stack variables. 
  When rendering without a depth target (e.g., color-only passes, 2D UI, post-processing where `m_target.depth` is `nullptr`), `desc.depthStencilAttachment` is never assigned. Because `desc` is uninitialized stack memory, `desc.depthStencilAttachment` contains a non-null garbage pointer. 
  Calling `m_encoder.BeginRenderPass(&desc)` causes Dawn/wgpu-native to dereference the wild pointer or fail validation immediately with a crash or abort. Furthermore, when `m_target.depth` is valid, fields like `depthAttachment.stencilLoadOp`, `depthAttachment.stencilStoreOp`, and `depthAttachment.depthReadOnly` remain uninitialized stack garbage, triggering non-deterministic validation failures across browsers.
- **Fix**: Value-initialize all descriptors using `{}` and explicitly set unused attachment pointers to `nullptr`:
  ```cpp
  wgpu::RenderPassDescriptor desc = {};
  wgpu::RenderPassColorAttachment colorAttachment = {};
  wgpu::RenderPassDepthStencilAttachment depthAttachment = {};
  
  colorAttachment.view = m_target.color;
  colorAttachment.loadOp = colorLoad;
  colorAttachment.storeOp = wgpu::StoreOp::Store;
  desc.colorAttachmentCount = 1;
  desc.colorAttachments = &colorAttachment;

  if (m_target.depth) {
      depthAttachment.view = m_target.depth;
      depthAttachment.depthLoadOp = depthLoad;
      depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
      desc.depthStencilAttachment = &depthAttachment;
  } else {
      desc.depthStencilAttachment = nullptr;
  }
  ```

---

### 2. Memory Safety: Null Pointer Dereference and Zero-Page Overwrite on Alloc Failure in `wfs_js_fetch`
- **File:Line**: `src/colobot-base/src/common/resources/web_archiver.c:112-124` (patch lines ~1805–1818)
- **Failure Scenario**:
  In `wfs_js_fetch`, memory is allocated on the WebAssembly heap using `_malloc`:
  ```javascript
  var ptr = _malloc(Math.max(bytes.byteLength, 1));
  HEAPU8.set(bytes, ptr);
  setValue(out, ptr, 'i32');
  setValue(out_len, bytes.byteLength, 'i32');
  return 0;
  ```
  If `_malloc` fails (e.g., under heap pressure when fetching multi-megabyte soundbanks, data archives, or textures), `ptr` is `0` (`NULL`). 
  `HEAPU8.set(bytes, 0)` executes and writes the entire asset payload starting at wasm address `0`. This destroys the Emscripten zero-page memory, corrupts NULL pointer safety checks, and corrupts runtime static data. 
  Then `wfs_js_fetch` returns `0` (indicating success) with `*out = NULL` and `*out_len > 0`. The caller `wa_openRead` assigns this to `mio_make(buf, len, ...)` and attempts to read from it, causing crashes and heap corruption.
- **Fix**: Check `ptr !== 0` before writing; if allocation fails, return an error code (e.g., `-1`) and set `*out = 0`, `*out_len = 0`:
  ```javascript
  var ptr = _malloc(Math.max(bytes.byteLength, 1));
  if (!ptr) {
      setValue(out, 0, 'i32');
      setValue(out_len, 0, 'i32');
      return -1;
  }
  HEAPU8.set(bytes, ptr);
  setValue(out, ptr, 'i32');
  setValue(out_len, bytes.byteLength, 'i32');
  return 0;
  ```

---

### 3. Filesystem / Data Loss: IDBFS Save Game Loss on Tab Close and Missing Sync Points
- **File:Line**: `src/colobot-base/src/common/resources/web_archiver.c:135-142` (patch lines ~1825–1835), `src/colobot-base/src/level/player_profile.cpp`, `src/colobot-base/src/level/robot.cpp`
- **Failure Scenario**:
  `web_save_sync()` (`FS.syncfs(false, ...)`) is only invoked inside `CConfigFile::Save()` and `CRobotMain::IOWriteScene()`.
  It is **not** called when:
  1. The user writes or modifies CBot programs in the editor (`robot.cpp` / user program files).
  2. The player customizes character appearance (`face.gam` in `player_profile.cpp`).
  3. Missions or exercises are completed and progress/trophies are recorded.
  
  The only fallback is an asynchronous `pagehide` / `beforeunload` listener in `loader.js`. Modern browsers (Chrome/Firefox/Safari) aggressively terminate workers and page processes during navigation/closure; asynchronous IndexedDB write transactions initiated inside `pagehide`/`visibilitychange` are **not guaranteed to complete**. When a player writes code or completes a level and closes the tab, all unsaved files written to the MEMFS `/user` partition are permanently lost.
- **Fix**:
  1. Hook `web_save_sync()` into all file write completion paths in `CResourceManager::CreateDirectory`, profile updates, and CBot program file saves.
  2. Implement a debounce timer in JS (`scheduleSync()`) that commits `FS.syncfs(false)` 500ms after any file modification in `/user`.
  3. For tab closure, register a synchronous `visibilitychange` handler that uses `navigator.locks` or triggers immediate sync when the tab loses focus, rather than relying exclusively on `pagehide`.

---

## Major Findings

### 4. Service Worker: Race Condition Bypasses Asset Cache on Initial Launch
- **File:Line**: `web/sw.js:145-156` (patch lines ~3310–3325)
- **Failure Scenario**:
  In `sw.js`:
  ```javascript
  const pending = getHashes();
  const table = await Promise.race([pending, Promise.resolve(null)]);
  ```
  `Promise.race([pending, Promise.resolve(null)])` executes immediately. Because `pending` (`fetch('/manifest.json')`) is asynchronous, `Promise.resolve(null)` **always settles first in the microtask queue**. 
  Therefore, `table` evaluates to `null` for every asset requested while `manifest.json` is in flight during initial boot. 
  When `table === null`, the worker bypasses hash checks, falls back to `fetch(req)`, and fails to store assets in the `colobot-data` cache. Every initial asset load bypasses the service worker cache entirely.
- **Fix**: Await `getHashes()` directly; do not race it with an immediate null resolution:
  ```javascript
  let table = null;
  try {
      table = await getHashes();
  } catch (err) {
      console.warn('Manifest load failed:', err);
  }
  ```

---

### 5. Service Worker / Cache Invalidation: Orphaned Cache Accumulation Across Deploys
- **File:Line**: `web/sw.js:12-25` (patch lines ~2780–2795), `web/sw.js:160-175`
- **Failure Scenario**:
  1. `let hashesP = null;` caches the manifest promise in the service worker global scope indefinitely. When a new deployment occurs and the service worker updates, any cached state in active worker instances continues serving old asset hashes until the SW process terminates.
  2. Data assets are stored under `key = url.origin + url.pathname + '?h=' + hash`. While the static cache (`CACHE_NAME = 'colobot-v1'`) is cleared on `activate`, the `colobot-data` cache is **never** pruned. As game files, textures, and models are updated across versions, old versions remain in `colobot-data` indefinitely, bloating client disk storage until storage quotas are exceeded.
- **Fix**:
  1. Clear or refetch `hashesP` on SW `activate`.
  2. During SW activation, iterate through keys in `colobot-data`, compare the stored hash against the current `manifest.json`, and delete entries not present in the current manifest.

---

### 6. Security: Unsanitized Query String Injection into Engine Command Line
- **File:Line**: `src/colobot-app/src/main.cpp:24-60` (patch lines ~533–570)
- **Failure Scenario**:
  `colobot_web_query_string()` takes `window.location.search` without validation, splits parameters on `&` and `=`, and passes them directly to `argv` as engine CLI flags:
  ```cpp
  args.push_back(strdup(("-" + key).c_str()));
  args.push_back(strdup(val.c_str()));
  ```
  An attacker can craft a link like `https://colobot.site/?savedir=/tmp&mod=../../malicious&batch=1&loglevel=trace` or pass arbitrary Colobot CLI flags (e.g., `--userdir`, `--mod`, `--setup`, `--batch`). This can divert save paths, override game rules, trigger unauthorized debug actions, or crash the runtime.
- **Fix**: Implement an explicit allowlist of permitted query parameters (e.g., `lang`, `level`, `profile`), reject unexpected parameters, and validate that values match strict alphanumeric/path character sets before translating them to `argv`.

---

### 7. JSPI / Async Suspension: Non-Reentrant Event Loop Execution During Fetch
- **File:Line**: `src/colobot-base/src/common/resources/web_archiver.c:115` (patch line ~1810), `src/colobot-base/src/app/app.cpp:240-270`
- **Failure Scenario**:
  Under Emscripten JSPI (`-sASYNCIFY=2`), calling `wfs_js_fetch` suspends the WebAssembly stack while waiting for `fetch()` to fulfill. 
  While suspended, the browser's JavaScript event loop resumes running. If an input event (e.g. mouse click, resize, keyboard stroke) or `requestAnimationFrame` fires while the engine is suspended inside `PHYSFS_openRead()` during scene load or resource streaming, the engine's event callbacks or main loop ticks can re-enter engine state while an existing frame or load operation is paused. 
  Because Colobot's engine architecture is not re-entrant, this leads to state corruption, double-frees, or corrupted physics/rendering states.
- **Fix**: In `loader.js` and `app.cpp`, introduce an engine-level suspension lock:
  ```javascript
  let isEngineSuspended = false;
  // Set isEngineSuspended = true before JSPI suspension and false upon resume.
  // In DOM/SDL event listeners and requestAnimationFrame, if isEngineSuspended is true,
  // queue or ignore incoming events instead of dispatching to wasm.
  ```

---

### 8. WebGPU / Window Management: Layout Resize Loops Between SDL and Canvas Observer
- **File:Line**: `src/colobot-base/src/app/app.cpp:115-135` (patch lines ~755–775, ~814–825), `web/loader.js:45-60`
- **Failure Scenario**:
  `web/loader.js` sets canvas CSS style `width: 100vw; height: 100vh` and uses a `ResizeObserver` to set `canvas.width = Math.floor(rect.width * dpr); canvas.height = Math.floor(rect.height * dpr);`.
  However, in `CApplication::CreateVideoSurface` and `SyncCanvasSize`, `SDL_SetWindowSize(m_private->window, cw, ch)` is called with the canvas pixel size.
  In Emscripten's SDL2 implementation, calling `SDL_SetWindowSize` writes directly to `canvas.style.width` and `canvas.style.height` using pixel dimensions (e.g. `canvas.style.width = "1920px"`), overriding the CSS responsive sizing and triggering a feedback loop with the `ResizeObserver` or distorting pixel aspect ratios.
- **Fix**: In `app.cpp` under `__EMSCRIPTEN__`, do not invoke `SDL_SetWindowSize` during canvas sync; instead, query the canvas internal pixel buffer size directly and update WebGPU swapchain target dimensions and viewport without modifying the SDL/DOM window styling.

---

## Minor Findings

### 9. PhysFS Web Archiver: `mio_duplicate` Discards Stream Seek Position
- **File:Line**: `src/colobot-base/src/common/resources/web_archiver.c:165-175` (patch lines ~1868–1878)
- **Failure Scenario**:
  `mio_duplicate(PHYSFS_Io* io)` calls `mio_make(m->buf, m->len, m->refs)`. `mio_make` initializes `pos` to `0`. 
  If upstream PhysFS or a mod/sound loader duplicates a file handle that has already been seeked or partially read, the cloned `PHYSFS_Io` resets its position to `0` instead of inheriting `m->pos` from the source handle.
- **Fix**: Copy `m->pos` to the duplicated handle:
  ```c
  static PHYSFS_Io* mio_duplicate(PHYSFS_Io* io) {
      MioHandle* m = (MioHandle*)io->opaque;
      PHYSFS_Io* copy = mio_make(m->buf, m->len, m->refs);
      if (copy) {
          ((MioHandle*)copy->opaque)->pos = m->pos;
      }
      return copy;
  }
  ```

---

### 10. PhysFS Web Archiver: `PHYSFS_Io` Leak in `wa_openArchive`
- **File:Line**: `src/colobot-base/src/common/resources/web_archiver.c:190-205` (patch lines ~1894–1905)
- **Failure Scenario**:
  `wa_openArchive(PHYSFS_Io* io, const char* name, int forWriting, int* claimed)` sets `*claimed = 1` and returns `&g_archiveOpaque` when `name` matches `/manifest.json`, but it never retains `io` or calls `io->destroy(io)`.
  Per PhysFS Archiver specifications, when an archiver claims an archive stream, it assumes ownership and must destroy `io` in `closeArchive`. Because `wa_closeArchive` does nothing with `io`, the underlying `PHYSFS_Io` allocated by PhysFS is leaked.
- **Fix**: Destroy `io` when claimed in `wa_openArchive` if not required:
  ```c
  if (strcmp(name, "/manifest.json") == 0 || strcmp(name, "manifest.json") == 0) {
      *claimed = 1;
      io->destroy(io);
      return &g_archiveOpaque;
  }
  ```

---

### 11. WebGPU: Dynamic Uniform Buffer Alignment
- **File:Line**: `src/colobot-base/src/graphics/webgpu/wgpu_buffer.cpp:80-95`
- **Failure Scenario**:
  Uniform buffer dynamic offsets in WebGPU require alignment to `minUniformBufferOffsetAlignment` (which is typically 256 bytes on modern adapters, but can vary by device). Offsets are hard-coded to multiples of 256 bytes in some passes without verifying device capabilities from `wgpu::Limits`.
- **Fix**: Query `wgpu::SupportedLimits` from the device and calculate dynamic offsets aligned to `deviceLimits.limits.minUniformBufferOffsetAlignment`:
  ```cpp
  uint32_t align = m_deviceLimits.minUniformBufferOffsetAlignment;
  uint32_t alignedSize = (size + align - 1) & ~(align - 1);
  ```

---

## Verdict

VERDICT: BLOCKING
