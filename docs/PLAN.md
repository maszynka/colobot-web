# Colobot Web Port Plan (Emscripten 6.0.13 + WebGPU + JSPI)
Author: Opus planning agent, 2026-11-01. Inputs: docs/analysis/*.md.

## Decisions
- Vendor upstream into `src/` in-tree (record commit in UPSTREAM.md); no patches dir.
- Async: `-sJSPI` (Chrome 137+, Firefox 139+). No ASYNCIFY build; unsupported browsers get a fallback page.
- Translations: English only for M1/M2; i18n-tools deferred to M3.
- Images: PNG (oxipng) for M1/M2; M3 → WebP via own loader (stb_image / createImageBitmap). No KTX2.
- Audio: WAV → Ogg Vorbis q4, decoded with stb_vorbis (replaces libsndfile). Music .ogg streamed lazily.

## emcc flags
- Compile: `-O3 -flto -fwasm-exceptions --use-port=sdl2 --use-port=sdl2_image:formats=png --use-port=sdl2_ttf --use-port=zlib --use-port=libpng --use-port=emdawnwebgpu`
- Link: `-sJSPI -sJSPI_IMPORTS=[...] -sJSPI_EXPORTS=[main,...] -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=2GB -sSTACK_SIZE=1MB -sENVIRONMENT=web -sMODULARIZE -sEXPORT_ES6 -lidbfs.js -lopenal -sEXIT_RUNTIME=0 --closure 1`
- Debug: `-O1 -g -sASSERTIONS=2 -sSAFE_HEAP`

## Main loop / JSPI
- `CApplication::Run` (app.cpp:1041) → `Frame()`; native keeps `while(Frame())`; web uses `emscripten_set_main_loop_arg(frame, app, 0, false)`.
- JSPI-suspending imports: WebGPU adapter/device request (once, before loop), `web_fetch_file(path)` from PhysFS lazy archiver, `idbfs_sync`.
- Threads removed under `__EMSCRIPTEN__`: CWorkerThread inline, music loading deferred per frame, screenshot synchronous. Drop SDL_Delay/SDL_WaitEvent.

## Work packages (same phase = disjoint files)
### Phase 0 (serial)
- WP0 Bootstrap: vendor source, top-level CMake, cmake/systems/Emscripten.cmake, nlohmann json, DESKTOP/TESTS/TOOLS off, NullDevice link.
### Phase 1 (parallel)
- WP1 Deps: PhysFS cross-compile (third_party/physfs), GLEW/OpenGL overrides, gettext stub, system_emscripten.cpp.
- WP2 Sound: vorbis_wav_reader (stb_vorbis + WAV) replacing sndfile_wrapper; oalsound/buffer.cpp; single-thread alsound.
- WP3 Main loop/app: app.cpp/app.h Frame split, remove SDL GL, WebGPU canvas surface; main.cpp; URL-query args.
- WP4 WebGPU core: graphics/webgpu/ wgpu_device, wgpu_util (FAN/LOOP/STRIP→lists, POINTS→quads), wgpu_pipeline_cache, wgpu_ring_buffer (256B dynamic offsets), wgpu_frame (pass manager), wgpu_texture (RGB→RGBA, CPU mips, subrect updates), wgpu_framebuffer (Depth32Float shadow atlas 2048; MSAA4 resolveTarget), async readback. Shared WGSL preamble/lighting/shadow.
- WP5 Asset tool: tools/build-data/*.mjs — installed subset, English only, oxipng, WAV→ogg, music download, manifest.json, boot.pak (≤15MB zip store), chapter packs + loose files.
- WP6 Web shell: web/index.html, loader.js (WebGPU+JSPI detection, progress, instantiateStreaming, audio unlock), sw.js, _headers, wrangler.jsonc (25 MiB/file limit).
### Phase 2 (parallel, after WP4)
- WP7 UI renderer, WP8 Terrain (textureSampleCompare, depthBias), WP9 Object, WP10 Particle+Shadow (WGSL ports).
- WP11 Lazy FS: web_archiver PHYSFS_Archiver from manifest, JSPI fetch on openRead; IDBFS /save + syncfs.
### Phase 3
- WP12 Integration + Playwright harness (WebGPU Chrome), menu screenshot, mission 1 via URL arg.
- WP13 Fast-load: WebP, prefetch next chapter, i18n, wasm-split if >8MB, SW precache.

## Milestones
- M1 menu (~90 agent-h), M2 mission (~60 h), M3 fast load: first paint <1.5s, menu <4s @50Mbps (~30 h). Total ~180 h / ~9k LOC.

## Risks
CBot exceptions need wasm EH (JSPI-compatible); FAN/LOOP draw overhead → batch; pipeline explosion → prewarm; headless WebGPU flaky; GL→WebGPU clip z remap [-1,1]→[0,1]; CF 25 MiB/file; music host rate limits → cache in R2.
