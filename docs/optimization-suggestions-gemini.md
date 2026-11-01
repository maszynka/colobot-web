A detailed implementation plan has been written to the [optimization plan artifact](local-file). Below is the prioritized analysis and concrete optimization plan for the Colobot browser port.

---

# Colobot Web: Prioritized Performance Optimization Plan

### 1. Bypass Blank 9-Second Welcome Sequence (`CScreenWelcome`) on Web
* **What**: On `__EMSCRIPTEN__`, immediately bypass or auto-dismiss `CScreenWelcome` to transition directly to the player dialog and main menu. Currently, the engine runs three consecutive 3.0-second welcome screens that draw no splash image (because `intro*.png` was omitted from data packaging in [`build.mjs`](../tools/build-data/build.mjs#L80)) while swallowing user input.
* **Why**: **Direct reduction of ~8,500 ms** in time-to-interactive menu (drops from ~9.5 s to ~1.0 s). 0 bytes transferred.
* **Exact files**: [`src/colobot-base/src/app/app.cpp`](../src/colobot-base/src/app/app.cpp#L1040-L1150), `src/colobot-base/src/ui/screen/screen_welcome.cpp`
* **Risk**: **Very low**. The splash images are already excluded from the web distribution; `CScreenWelcome` is an empty delay on the web.

---

### 2. Lean `boot.pak` Layout: Evict Non-Essential Menu Assets
* **What**: Restrict `boot.pak` strictly to assets required for first paint and the initial player dialog / main menu screen (fonts, cursor, core UI skin, button icons, essential shaders). Move level preview images (`levels/*/*.png`, thumbnails) and secondary interface art into loose files or level packs.
* **Why**: Shrinks `boot.pak` from **9.3 MB down to ~2.0–2.5 MB** (~70% reduction, saving **~7 MB raw / ~2.5 MB gzipped**). On a standard 30 Mbps connection, this shaves **~600–1,200 ms** off the download and `DecompressionStream` decompression pipeline, directly advancing time-to-first-paint.
* **Exact files**: [`tools/build-data/build.mjs`](../tools/build-data/build.mjs#L78-L96)
* **Risk**: **Low**. If an asset removed from `boot.pak` is accessed later, [`web_archiver.c`](../src/colobot-base/src/common/resources/web_archiver.c#L116-L137) falls back to fetching it on demand from the manifest.

---

### 3. Pre-compress Assets & Wasm with Brotli (.br) Beside/Instead of Gzip
* **What**: Pre-compress `colobot.wasm`, `boot.pak`, and loose files with Brotli (`brotli -q 11` or Node `zlib.brotliCompressSync`) during data assembly. Serve with `Content-Encoding: br` in static server configs / `_headers`, and fall back to `DecompressionStream('deflate-raw')` or gzip if needed.
* **Why**: Brotli q11 provides 15–25% higher compression ratio over gzip -9 on Wasm and archive data. Saves **~400 KB** on `colobot.wasm` (1.6 MB -> ~1.2 MB) and **~600 KB–1 MB** on `boot.pak`, saving **~200–500 ms** in wire transfer time on cold loads.
* **Exact files**: [`tools/build-data/build.mjs`](../tools/build-data/build.mjs#L129-L135), [`scripts/assemble-dist.sh`](../scripts/assemble-dist.sh), [`web/loader.js`](../web/loader.js#L64-L68), [`web/_headers`](../web/_headers#L5-L10)
* **Risk**: **Very low**. Every modern browser supporting WebGPU and JSPI supports Brotli and `DecompressionStream`.

---

### 4. HTML Preload Hints for Startup Critical Path
* **What**: Add `<link rel="modulepreload" href="loader.js">`, `<link rel="modulepreload" href="colobot.js">`, `<link rel="preload" href="colobot.wasm" as="fetch" crossorigin>`, `<link rel="preload" href="data/boot.pak.gz" as="fetch" crossorigin>`, and `<link rel="preload" href="data/manifest.json" as="fetch" crossorigin>` directly to `<head>` in `index.html`.
* **Why**: Eliminates sequential discovery delay where the browser parses HTML, fetches `loader.js`, executes it, and only then starts the downloads for Wasm and data packs. Saves **~150–350 ms** (1 network RTT + JS parse time) on time-to-first-paint.
* **Exact files**: [`web/index.html`](../web/index.html#L3-L8)
* **Risk**: **None**. Standard declarative resource hints.

---

### 5. Fix Service Worker Manifest-Blocking Fetch on Data Requests
* **What**: In `web/sw.js`, `loadHashes()` intercepts data requests and performs `await fetch('data/manifest.json', { cache: 'no-cache' })` over the network before allowing any `/data/*` request to proceed. Refactor `sw.js` to either precache/inline hashes during SW activation or stream asset requests without waiting on `manifest.json`.
* **Why**: Removes a blocking **~50–200 ms** network round-trip on cold start and offline loads. Avoids SW interception stall on initial `boot.pak.gz` download.
* **Exact files**: [`web/sw.js`](../web/sw.js#L6-L20)
* **Risk**: **Low to medium**. Requires verifying cache invalidation when deploying new manifest versions.

---

### 6. Compiler Optimization Profile: `-Oz`, `-flto`, and Closure Compiler
* **What**: Enable `COLOBOT_WEB_LTO=ON`, switch `COLOBOT_WEB_OPT` from `-O3` to `-Oz` (or `-Os`), and enable `COLOBOT_WEB_CLOSURE=ON` (`--closure 1`) in `CMakeLists.txt`.
* **Why**: `-Oz` + `-flto` reduces Wasm binary size by **20–35%** (saving **1.0–1.5 MB uncompressed**, **~300–500 KB compressed**), while Closure shrinks `colobot.js` glue code by **~50–100 KB**. Decreases initial download time by **~150–400 ms** and speeds up V8 Liftoff / TurboFan compilation and memory usage.
* **Exact files**: [`CMakeLists.txt`](../CMakeLists.txt#L34-L59), [`CMakeLists.txt`](../CMakeLists.txt#L84-L86)
* **Risk**: **Low to medium**. Longer link times; must verify that `-Oz` does not adversely degrade CPU-heavy CBot simulation or physics loops compared to `-O3`.

---

### 7. Match System Preferred Canvas Format (`bgra8unorm`)
* **What**: Accurately detect and apply `navigator.gpu.getPreferredCanvasFormat()` (`bgra8unorm` on macOS and most Chromium platforms) in [`wgpu_device.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_device.cpp#L228-L237) and align framebuffer targets.
* **Why**: Resolves Chrome warning: *"Canvas format 'rgba8unorm' is not the preferred canvas format 'bgra8unorm' for this system; an extra format conversion copy will occur every frame"*. Eliminates a full-frame GPU blit/conversion copy on **every frame**, saving **~0.3–0.8 ms per frame** at 1080p and **~1.5–2.0 ms per frame** at retina/4K resolutions.
* **Exact files**: [`src/colobot-base/src/graphics/webgpu/wgpu_device.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_device.cpp#L228-L237), [`src/colobot-base/src/graphics/webgpu/wgpu_framebuffer.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_framebuffer.cpp#L50-L56)
* **Risk**: **Low**. Must verify that pipeline fragment color formats and offscreen swapchain targets match `SurfaceFormat()`.

---

### 8. GPU-Accelerated Mipmap Generation (Replace CPU Box-Filter Downsampling)
* **What**: In [`wgpu_texture.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_texture.cpp#L120-L135), replace `DownsampleRGBA` (a software 4-tap CPU box filter loop that calls `m_queue.WriteTexture` per mip level) with a GPU render pass using a fullscreen triangle and hardware bilinear filtering to downsample mip $N$ into mip $N+1$.
* **Why**: Texture upload and mip generation time drops from **10–50 ms per 1024x1024 texture on CPU to < 0.2 ms on GPU**. For mission loading (which loads dozens of terrain and object textures), this shaves **~300–800 ms** in `CRobotMain::CreateScene` and eliminates heap churn in `std::vector<uint8_t>`.
* **Exact files**: [`src/colobot-base/src/graphics/webgpu/wgpu_texture.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_texture.cpp#L120-L135), [`src/colobot-base/src/graphics/webgpu/wgpu_texture.h`](../src/colobot-base/src/graphics/webgpu/wgpu_texture.h), [`src/colobot-base/src/graphics/webgpu/wgpu_util.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_util.cpp#L183-L205)
* **Risk**: **Low**. Textures must be created with `wgpu::TextureUsage::RenderAttachment` (already enabled for mips).

---

### 9. Prewarm WebGPU Render Pipelines Ahead of Gameplay
* **What**: Add an explicit `Prewarm()` method in [`CWebGPUPipelineCache`](../src/colobot-base/src/graphics/webgpu/wgpu_pipeline_cache.h#L67-L79) and invoke it during device creation or during the mission loading screen. Compile the standard combinations of `PipelineKey` (UI blend/alpha, terrain default, object diffuse + shadow, particle additive/blend) before the first frame runs.
* **Why**: `device.CreateRenderPipeline` is synchronous and takes **10–50 ms per pipeline**. Prewarming eliminates severe **100–300 ms frame drops / hitching** when the first 3D objects, terrain tiles, shadows, and particles appear on screen.
* **Exact files**: [`src/colobot-base/src/graphics/webgpu/wgpu_pipeline_cache.h`](../src/colobot-base/src/graphics/webgpu/wgpu_pipeline_cache.h), [`src/colobot-base/src/graphics/webgpu/wgpu_pipeline_cache.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_pipeline_cache.cpp), [`src/colobot-base/src/graphics/webgpu/wgpu_device.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_device.cpp#L270-L280)
* **Risk**: **Very low**. Compiles existing pipelines slightly earlier during idle/loading rather than just-in-time during draw calls.

---

### 10. Eliminate Loose-File JSPI Cascades During Level Load
* **What**: In [`tools/build-data/build.mjs`](../tools/build-data/build.mjs#L136-L145), avoid splitting level/chapter assets into loose files simply because they exceed `LOOSE_MIN` (64 KiB). Keep chapter models and textures inside `lvl-<mission>-<chapter>.pak`. In [`web_archiver.c`](../src/colobot-base/src/common/resources/web_archiver.c#L116-L137), implement parallel prefetching for chapter packs prior to `CreateScene`.
* **Why**: When loading a mission, the engine currently suspends via JSPI on dozens of individual loose file `openRead` calls sequentially. Bundling them into the chapter pack turns a **1,500–3,000 ms serial network latency waterfall** into a single streamed HTTP download.
* **Exact files**: [`tools/build-data/build.mjs`](../tools/build-data/build.mjs#L136-L145), [`src/colobot-base/src/common/resources/web_archiver.c`](../src/colobot-base/src/common/resources/web_archiver.c#L49-L78), [`web/loader.js`](../web/loader.js#L143-L148)
* **Risk**: **Low to medium**. Must ensure chapter pack sizes remain below Cloudflare's 25 MiB file threshold.

---

### 11. Native Response Streaming for Wasm Liftoff Compilation & Caching
* **What**: In `web/loader.js`, pass the native `fetch('colobot.wasm')` `Response` directly to `WebAssembly.instantiateStreaming(resp, imports)` without wrapping it in a custom JS `new ReadableStream`. Use a `TransformStream` or alternative byte estimator for the loading bar.
* **Why**: Custom JS `ReadableStream` wrapping triggers an `ERR_ABORTED` in Chromium network logs, defeats V8's native C++ streaming decoder (preventing Liftoff from compiling bytecode concurrently while downloading), and prevents Chromium from storing the compiled module in the HTTP code cache. Shaves **~200–400 ms** compile latency on first run and achieves near-instant compilation on reload.
* **Exact files**: [`web/loader.js`](../web/loader.js#L21-L38), [`web/loader.js`](../web/loader.js#L89-L94)
* **Risk**: **Low**. Requires adjusting the progress bar calculation for the Wasm portion.

---

### 12. WebP Texture Compression for 3D Models & Environments
* **What**: In `tools/build-data/build.mjs`, convert large PNG textures (models, textures, backgrounds) to WebP (lossless for interface, high-quality lossy q85 for 3D textures). Decode in engine via `createImageBitmap` in JS or `stb_image` / `libwebp`.
* **Why**: Reduces texture asset footprint across `models.pak`, `common.pak`, and loose files by **40–60%** (saving **10–15 MB** across the game, and **~4–8 MB** on mission 1 load). Reduces network transfer time and memory footprint.
* **Exact files**: [`tools/build-data/build.mjs`](../tools/build-data/build.mjs#L55-L72), [`src/colobot-base/src/common/image.cpp`](../src/colobot-base/src/common/image.cpp), [`web/loader.js`](../web/loader.js)
* **Risk**: **Medium**. Requires updating image loaders to support WebP decoding on the web.

---

### 13. Multi-Frame Ring Buffer Staging (Avoid In-Flight Buffer Overwrite Stalls)
* **What**: In [`wgpu_ring_buffer.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_ring_buffer.cpp#L48-L62), transition `CWebGPURingBuffer` to a true 2- or 3-frame staging ring. Currently, `BeginFrame()` resets `chunk->used = 0` every frame, causing `WriteBuffer(chunk->buffer, 0, ...)` to write to offset 0 while the previous frame may still be in-flight in the GPU queue.
* **Why**: Prevents WebGPU driver pipeline stalls and eliminates internal shadow copies in Blink/Dawn. Also, only flushing dirty subranges in `Flush()` avoids repeatedly transferring up to 5 MB across the CPU-GPU boundary, saving **~0.5–1.0 ms CPU/GPU sync time per frame**.
* **Exact files**: [`src/colobot-base/src/graphics/webgpu/wgpu_ring_buffer.h`](../src/colobot-base/src/graphics/webgpu/wgpu_ring_buffer.h#L40-L70), [`src/colobot-base/src/graphics/webgpu/wgpu_ring_buffer.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_ring_buffer.cpp#L48-L99), [`src/colobot-base/src/graphics/webgpu/wgpu_device.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_device.cpp#L406-L420)
* **Risk**: **Low**. Standard double/triple buffering ring pattern.

---

### 14. WebGPU State Caching & Redundant Bind Group Elimination
* **What**: In [`wgpu_pass_manager.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_pass_manager.cpp#L87-L100) and renderers ([`wgpu_object_renderer.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_object_renderer.cpp#L404-L418)), cache the currently bound pipeline handle, dynamic uniform offsets, and bind group handles (e.g. `group(0)` scene bindings). Skip redundant calls to `pass.SetPipeline` and `pass.SetBindGroup`.
* **Why**: In frames with many objects and terrain draws, eliminates hundreds of redundant WebGPU command encoding and validation calls, saving **~0.8–1.5 ms CPU time per frame** in the render loop.
* **Exact files**: [`src/colobot-base/src/graphics/webgpu/wgpu_pass_manager.h`](../src/colobot-base/src/graphics/webgpu/wgpu_pass_manager.h), [`src/colobot-base/src/graphics/webgpu/wgpu_pass_manager.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_pass_manager.cpp), [`src/colobot-base/src/graphics/webgpu/wgpu_object_renderer.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_object_renderer.cpp#L404-L418), [`src/colobot-base/src/graphics/webgpu/wgpu_ui_renderer.cpp`](../src/colobot-base/src/graphics/webgpu/wgpu_ui_renderer.cpp#L189-L219)
* **Risk**: **Very low**. Simple state equality check before issuing encoding commands.

---

### 15. Binaryen `wasm-split` for Deferred Game Subsystems
* **What**: Integrate Binaryen's `wasm-split` into the build process to divide `colobot.wasm` into a primary module (engine core, renderer, UI, menu, audio) and a secondary module (CBot compiler/interpreter, script debugger, level editor, physics sub-solvers). Load the secondary module asynchronously in the background via `loadSplitModule`.
* **Why**: Reduces the initial `colobot.wasm` binary from **4.6 MB down to ~2.0–2.5 MB** (~50% reduction in initial Wasm payload), cutting initial download and compilation time by **~200–400 ms** on cold boot.
* **Exact files**: [`CMakeLists.txt`](../CMakeLists.txt#L64-L91), [`scripts/assemble-dist.sh`](../scripts/assemble-dist.sh), [`web/loader.js`](../web/loader.js#L89-L95)
* **Risk**: **High**. Requires profiling instrumented symbol call graphs to ensure no secondary module code is invoked synchronously before the split module has resolved.
