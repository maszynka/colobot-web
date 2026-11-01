# Review of docs/PLAN.md (Colobot → Emscripten 6.0.13 + WebGPU + JSPI)

Reviewer: Fable subagent, 2026-11-01. Method: every claim checked against
`upstream/colobot` source, `/opt/homebrew/Cellar/emscripten/6.0.13/libexec`
(settings.js, link.py, cmdline.py, src/lib/*.js, tools/ports/*, test/*), the
pinned emdawnwebgpu package (`v20260423.175430`, downloaded to scratchpad) and
SDL `release-2.32.10` (`src/video/emscripten/SDL_emscriptenvideo.c`, the version
the sdl2 port builds).

Legend: **B** = blocking (plan as written will not build/run), **C** = correction,
**OK** = verified, **M** = missing item.

---

## 1. Toolchain facts (verified)

| Claim | Status | Evidence |
|---|---|---|
| Emscripten 6.0.13 installed | OK | `emcc --version` → `6.0.13-git`; tools in `libexec/` |
| `-sJSPI`, `-sJSPI_IMPORTS`, `-sJSPI_EXPORTS` exist | OK | settings.js L931/940/950; `cmdline.py` L767: `JSPI` → `ASYNCIFY = 2`, `JSPI_IMPORTS` → `ASYNCIFY_IMPORTS`, `JSPI_EXPORTS` → `ASYNCIFY_EXPORTS`. `ASYNCIFY_EXPORTS` is deprecated in favour of `JSPI_EXPORTS`. `main` is a default JSPI export (link.py L1718). |
| `--use-port=emdawnwebgpu` exists | OK | `emcc --show-ports` lists it; it is a *remote* port that downloads `emdawnwebgpu_pkg-v20260423.175430.zip` from GitHub at first use (needs network + writable `libexec/cache/ports`; set `EM_CACHE` if Cellar is read-only in CI). |
| Ports: sdl2, sdl2_image, sdl2_ttf, zlib, libpng, ogg, vorbis, freetype, harfbuzz | OK | all in `--show-ports`. No port for libsndfile or PhysFS (plan's replacement/cross-compile is correct). |
| `-lopenal` / `-lidbfs.js` | OK | link.py L2690 maps `openal` → `libopenal.js`; `src/lib/libidbfs.js` exists. |
| `-fwasm-exceptions` + JSPI | OK (with caveat) | JSPI is VM-level stack switching, not the Binaryen Asyncify pass, so the Asyncify/EH incompatibility does not apply. No link.py check forbids the combo. **Caveat:** the shipped test suite has no test parameterised over JSPI × `-fwasm-exceptions`; add a 20-line probe (throw/catch across a suspended import) to WP0. Default is `WASM_LEGACY_EXCEPTIONS=1`; Chrome ≥137 supports both legacy and exnref. |
| `wgpuInstanceWaitAny` under JSPI | OK | `library_webgpu.js` L689-700: `emwgpuWaitAny` is defined under `#if ASYNCIFY` (true for ASYNCIFY=2) with `__async: true` and `Asyncify.handleAsync(...)`, which under JSPI is `await startAsync()`. `webgpu.cpp` L1644/L1719 gate `TimedWaitAnyEnable` on `emscripten_has_asyncify()`, which returns `{{{ ASYNCIFY }}}` = 2 → true. The non-asyncify path aborts (`TODO: Implement asyncify-free WaitAny for timeout=0`, L717), so **TimedWaitAny requires JSPI and works with it**. The instance must be created with `WGPUInstanceFeatureName_TimedWaitAny`. `emwgpuWaitAny` is auto-marked async by the library, no need to list it in `JSPI_IMPORTS`. |
| JSPI through function pointers / indirect calls | OK | settings.js L927-928: "IGNORE_INDIRECT etc. are not needed" under JSPI. Suspension works through any call chain **provided the wasm was entered via a promising export**. The PhysFS archiver vtable (`PHYSFS_Archiver.openRead` etc.) is reachable because PhysFS is called from `main()`/the frame function, both JSPI exports. |
| JSPI + `emscripten_set_main_loop_arg` | **C** | `libeventloop.js` L291-305 `runIter` calls `callUserCallback(func)` and does **not** await. `$dynCall`/`getWasmTableEntry` (libcore.js ~L1920-1995) wraps a table entry with `WebAssembly.promising` **only if** the function is in `Asyncify.asyncExports`, i.e. its *export name* matches `JSPI_EXPORTS` (libasync.js L162-171). Consequences: (a) the frame callback must be `EMSCRIPTEN_KEEPALIVE`-exported and listed in `JSPI_EXPORTS` (plan says `[main,...]` without naming it); (b) because runIter does not await the returned Promise, rAF will re-enter `frame()` while a previous frame is suspended on a fetch → re-entrant game loop. **Fix:** drop `emscripten_set_main_loop`; drive the loop from JS: `async function tick(){ await Module._colobot_frame(); if(!done) requestAnimationFrame(tick); }` with `-sJSPI_EXPORTS=[main,colobot_frame]`. Keeps one wasm activation at a time. |
| `-sMODULARIZE -sEXPORT_ES6 --closure 1` with JSPI | OK | link.py L1604 only disables wasm export-name minification under JSPI (harmless). emdawnwebgpu port injects its own Closure externs (`emdawnwebgpu.port.py` L202-205). |
| Browser support "Chrome 137+, Firefox 139+" | **C** | Emscripten's own browser tests mark JSPI as Chromium-only (`test_browser.py` L3197, L4867: "Only chromium-based browsers support JSPI today"); `common.py` L250 mentions Firefox but the JSPI-specific tests skip on it. Treat **Chromium ≥137 as the only supported target for M1/M2**; Firefox and Safari go to the fallback page. Verify Firefox WebGPU+JSPI empirically before advertising it. |

## 2. Flag errors in the plan (B/C)

1. **B** `-fwasm-exceptions`, `--use-port=...`, `-O3 -flto` are listed under
   "Compile" only. All of them must also be passed at **link** (ports add JS
   libraries and link-time settings at link; mixing `-fwasm-exceptions`
   objects with a link lacking the flag fails or silently links Emscripten-EH
   runtime). In CMake set them in both `CMAKE_CXX_FLAGS` and
   `CMAKE_EXE_LINKER_FLAGS` (or `target_compile_options` + `target_link_options`).
2. **C** `-sJSPI_EXPORTS=[main,...]` → must enumerate `colobot_frame` (see §1)
   and any other wasm function JS calls that may suspend (e.g. a
   `colobot_resize` that re-creates swap-chain textures does not suspend; a
   `colobot_sync_saves` that calls `idbfs_sync` does).
3. **C** `-sJSPI_IMPORTS=[...]` is unnecessary if the suspending imports are
   written with `EM_ASYNC_JS` or a JS library using `__async: true`
   (settings.js L947-948). Prefer `EM_ASYNC_JS` for `web_fetch_file`,
   `idbfs_sync`; this also keeps the import list out of CMake.
4. **C** emdawnwebgpu needs `-sUSE_WEBGPU=0` (default) and must **not** be
   combined with `-sUSE_WEBGPU=1`. Not an error in the plan, but WP0 should
   assert it.
5. **C** Debug flags `-O1 -g -sASSERTIONS=2 -sSAFE_HEAP` are fine; add
   `-sASYNCIFY_DEBUG=1` for JSPI tracing (works for ASYNCIFY=2).

## 3. Source facts vs plan

| Plan claim | Verdict | Evidence |
|---|---|---|
| "CBot exceptions need wasm EH" | **C** overstated for CBot, correct for the engine | CBot has **no** `throw`; only 2 `catch (std::out_of_range)` (`CBotExprLitChar.cpp:97`, `CBotExprLitString.cpp:124`) and 2 `catch(...)` (`stdlib/FileFunctions.cpp:51/370`). CBot's own `throw`/`catch` is a script-language feature implemented as a state machine (`CBotCatch.cpp`). C++ exceptions **are** load-bearing in the engine: 132 `throw`/`catch` sites in colobot-base, chiefly `level/parser/parserparam.cpp` (26), `robotmain.cpp` (12, `CLevelParserException`, `CObjectCreateException` rethrown at L3505), `model_txt.cpp`. Level loading is exception-driven → `-fwasm-exceptions` stays mandatory; `-fno-exceptions` is not an option. |
| `CApplication::Run` at app.cpp:1041, `while(true)`, `SDL_Delay`, `SDL_WaitEvent`, `goto end` | OK | L1041, L1061 (`SDL_Delay`), L1092 (`SDL_WaitEvent` when inactive), L1109/1129/1143 `goto end`, label at L1196. |
| GL leakage only in app.cpp | OK | `SDL_GL_*` only in app.cpp (L149, L452-462, L729-866). `CreateVideoSurface` hard-codes `SDL_WINDOW_OPENGL` (L719). |
| Factory `Gfx::CreateDevice` in `opengl33/glutil.cpp:61`, called `app.cpp:647` (and 654 fallback `"opengl"`) | OK | Declared in `opengl33/glutil.h:51`; `app.cpp` includes the opengl33 header → **WP3/WP4 coupling** (see §4). |
| Only backend opengl33, 13 GLSL files | OK | `graphics/opengl33/shaders/gl33/*.glsl` = 13 files; renderers load `preamble.glsl`, `shadow.glsl`, `lighting.glsl` + per-renderer vs/fs via `LoadSource()` over PhysFS. Shaders are **not in colobot-data**; they are installed from the source tree (`opengl33/CMakeLists.txt` L60 `install(DIRECTORY shaders/gl33 ...)`). WGSL must be shipped the same way (into boot.pak) or embedded as string literals (recommended: embed; removes a fetch and a PhysFS dependency during device init). |
| Threads: CWorkerThread (sound), app.cpp:1535, engine.cpp:566, event mutex | OK | `alsound.h:140 CWorkerThread m_thread` used at `alsound.cpp:151` (CacheMusic) and `:590` (PlayMusic); `app.cpp:1535` `std::thread{...}.detach()` in `StartLoadingMusic` (calls `m_sound->CacheAll()`); `engine.cpp:566` screenshot thread; `event.cpp:629/654` `std::mutex`. Without `-pthread`, `std::thread` ctor throws `std::system_error` at runtime → all three must be `#ifdef __EMSCRIPTEN__`'d. `std::mutex` is a harmless no-op. |
| libsndfile usage | OK | `common/resources/sndfile_wrapper.{h,cpp}` + `resourcemanager.{h,cpp}:134 GetSNDFileHandler` + `common/CMakeLists.txt`. Consumer is `sound/oalsound/buffer.cpp`. Plan's stb_vorbis + WAV replacement is right; note it touches **resourcemanager.h/.cpp** (also touched by WP11). |
| PhysFS API surface | OK | `PHYSFS_init/mount/setWriteDir/exists/stat/enumerateFiles/openRead/openAppend/seek/tell/fileLength/readBytes/writeBytes/close`. `Exists`/`stat`/`enumerateFiles` are hot (resourcemanager.cpp L144-251) → the lazy archiver must answer `stat`/`enumerate`/`exists` **synchronously from the manifest**; only `openRead` may suspend. Fetch whole file on `openRead` (CInputStreamBuffer reads in chunks; per-chunk fetch would multiply suspensions). |
| Readback users | OK | `engine.cpp:560` (screenshot) and `engine.cpp:3166` — check what 3166 is for before deciding readback can be async-only (if it is used for picking/mini-map, a synchronous JSPI `mapAsync` wait inside the frame is needed; it is legal since the frame is a JSPI export). |
| CMake: `cmake/systems/${CMAKE_SYSTEM_NAME}.cmake` OPTIONAL → Emscripten falls to no system file | **C** | `CMakeLists.txt:29` includes `systems/Emscripten.cmake` **if it exists** (Emscripten toolchain sets `CMAKE_SYSTEM_NAME=Emscripten`). The plan's `cmake/systems/Emscripten.cmake` is therefore the right hook (platform.md's "use Other" is wrong — `Other.cmake` is never picked automatically). |
| find_package list | OK | OpenGL, SDL2, SDL2_image, SDL2_ttf, PNG, Gettext, Intl, PhysFS, glm, nlohmann_json (falls back to empty `lib/json` submodule), GLEW, OpenAL, SndFile, plus `lib/localename`. Every one needs an override/stub under Emscripten; `lib/json` submodule is empty → fetch nlohmann (plan says so). `libpng` is used by `common/image.cpp` (PNG write for screenshots) — keep `--use-port=libpng`. gettext: `app.cpp`, `script/script.cpp`, `common/restext.cpp` → stub `gettext()` returning msgid + `bindtextdomain` no-ops. |
| SDL2 port coexists with WebGPU canvas | OK (with constraints) | SDL 2.32.10 `SDL_emscriptenvideo.c`: L217 canvas id **hard-coded `"#canvas"`** (no selector hint in 2.32); L252 GL/EGL setup only `if (window->flags & SDL_WINDOW_OPENGL)`; a non-GL window just sizes the canvas (`emscripten_set_canvas_element_size`, L229/241/291) and installs input handlers. So: create the SDL window **without** `SDL_WINDOW_OPENGL`, never call `SDL_GetWindowSurface`/`SDL_CreateRenderer` (would `getContext('2d')` and poison the canvas), then `wgpuInstanceCreateSurface` with `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector{"#canvas"}` (`library_webgpu.js` L1984-2002 calls `canvas.getContext('webgpu')`). On `SDL_WINDOWEVENT_SIZE_CHANGED` re-`wgpuSurfaceConfigure`. HiDPI: SDL sets canvas pixel size = css × `pixel_ratio` (L291); use `SDL_GL_GetDrawableSize` equivalent (`SDL_GetWindowSizeInPixels`) for the surface config. |
| OpenAL on main thread | OK | `libopenal.js` is pure WebAudio (`alcCreateContext` → `new AudioContext`, L2135; `autoResumeAudioContext` hooks the first user gesture, L2149). No worker/pthread involved; Colobot's per-frame `alGetSourcei` polling drives `AL.updateSources`. The plan's "audio unlock" in loader.js is still needed for the autoplay policy. |
| Blocking loads `CRobotMain::CreateScene` robotmain.cpp:2771, `CacheAll` sound.cpp:45 | OK | Both run inside `main()`/frame → JSPI-suspendable. |

## 4. File-disjointness of work packages — does it hold?

**No.** Conflicts found:

| Files | Packages | Resolution |
|---|---|---|
| `app/app.cpp`, `app/app.h` | WP3 (Frame split, remove SDL GL, WebGPU surface) and WP4 (device factory `Gfx::CreateDevice` is declared in `opengl33/glutil.h`, included by app.cpp; WebGPU surface creation needs the `WGPUInstance`/`WGPUSurface` from the device). | WP0 adds `graphics/core/device_factory.h` declaring `std::unique_ptr<CDevice> CreateDevice(const DeviceConfig&, const std::string&)` and a `struct PlatformWindow { const char* canvasSelector; int w, h; }`; opengl33 and webgpu both implement it. app.cpp includes only that header. WP4 owns surface creation (inside `CWebGPUDevice::Create()`), WP3 only passes the selector/size. |
| `common/CMakeLists.txt` | WP1 (system_emscripten.cpp, gettext stub) and WP2 (sndfile_wrapper removal) | Move all CMake edits to WP0/WP1; WP2 only adds files listed by name in a `COLOBOT_WEB_SOUND_SOURCES` variable that WP1 leaves for it. |
| `common/resources/resourcemanager.{h,cpp}` | WP2 (`GetSNDFileHandler` → new reader) and WP11 (mount web archiver) | WP2 replaces `GetSNDFileHandler`; WP11 must not edit resourcemanager — mount the archiver from `app/pathman.cpp`/`system_emscripten.cpp` via `PHYSFS_registerArchiver` + `PHYSFS_mount("manifest.json")`. |
| `graphics/webgpu/wgpu_device.cpp` | WP4 and WP7-10 (each renderer must be constructed/returned by `Get*Renderer()`) | WP4 ships five stub renderer classes with final file names (`wgpu_ui_renderer.{h,cpp}` …) that compile and draw nothing; WP7-10 only replace the bodies of their own files. |
| Shared WGSL `preamble/lighting/shadow` | WP4 owns them; WP8 (terrain), WP9 (object) both use lighting+shadow, WP10 (particle) uses shadow. | Freeze the shared WGSL **interface** in WP4 (uniform struct layouts, `fn shadow(...)`, `fn lighting(...)` signatures, binding groups 0/1 reserved) with a GLSL→WGSL 1:1 port of the three includes. WP7-10 may not edit them; needed changes go through WP4's owner after Phase 2 (serial fix-up pass WP10b). |
| `boot.pak` mount point | unassigned | Nobody mounts `boot.pak` (`PHYSFS_mountMemory`) nor the manifest archiver in M1. Assign to WP11 (move to Phase 1 — M1 menu cannot render without assets). |
| Manifest/pack format | WP5 (writer) vs WP11 (reader) | WP0 writes `docs/manifest-format.md` (JSON schema: path → {pack, offset, size, sha}) before Phase 1. |

Phase ordering correction: WP11 must be Phase 1 (M1 depends on it); WP7-10 remain Phase 2 but gain a serial WP10b "shared WGSL reconcile". WP3 depends on the factory header from WP0, not on WP4.

## 5. Missing items (M)

1. **M** JSPI re-entrancy guard / JS-driven loop (§1).
2. **M** Probe in WP0: `-fwasm-exceptions` + `-sJSPI` + `emdawnwebgpu` hello-triangle that throws through a suspended `wgpuInstanceWaitAny`. Fail-fast before 180 h of work.
3. **M** Mouse: `MoveMouse({0.5,0.5})` at Run start and camera control rely on `SDL_WarpMouseInWindow`, which is a no-op in browsers; the game's mouse-at-edge camera scroll needs checking (plan silent). Pointer lock only on gesture.
4. **M** Fullscreen toggle needs a user gesture (SDL emscripten `emscripten_request_fullscreen_strategy`, L364); config "fullscreen=1" at startup will fail silently.
5. **M** `std::thread` removal in `engine.cpp:566` (screenshot) is listed; `event.cpp` mutex is fine; but **`CWorkerThread` header includes `<thread>`** and constructs a thread in its ctor → `alsound.h:140` member must be replaced by an inline executor, not just left unused.
6. **M** Sound `CacheAll` (83 WAV → ogg) decoded synchronously at startup on main thread: budget ~0.3–1 s of stb_vorbis for 7 MB PCM. Decode on first `Play` instead (fits "fast load" goal).
7. **M** Save/config: `GetSaveDir` (`system.cpp:218`) → `/save` IDBFS; `PHYSFS_setWriteDir` must point there; `colobot.ini` also lives there; call `idbfs_sync` after each save, on `visibilitychange` and `pagehide`.
8. **M** gettext stub + `lib/localename` removal; `find_package(Intl)` override.
9. **M** `-sENVIRONMENT=web` excludes Workers → `sw.js` must be plain JS, not an Emscripten output (plan is consistent, make explicit).
10. **M** Emscripten port downloads (sdl2, sdl2_image, sdl2_ttf, freetype, harfbuzz, libpng, zlib, emdawnwebgpu) require network on first build; pre-warm with `embuilder build ...` in CI and cache `EM_CACHE`.
11. **M** `WGPUInstanceFeatureName_TimedWaitAny` must be requested in `wgpuCreateInstance`, otherwise `wgpuInstanceWaitAny(timeout>0)` aborts (`webgpu.cpp` L1706-1719).
12. **M** Depth range: GL `[-1,1]` → WebGPU `[0,1]` is in Risks; it must be fixed in the **projection matrix** (`CEngine`/`Math::LoadProjectionMatrix`) or in the WGSL vertex stage consistently for all five renderers — assign to WP4 (shared WGSL preamble `fn fix_clip()`), not to each WP7-10.
13. **M** Firefox/Safari fallback copy (§1): M1 ships Chromium-only.
14. **M** Milestone budget: 180 agent-h assumes no re-review loops; the JSPI/WGSL probes above add ~6 h but de-risk the rest.

## 6. Corrected, ordered package list

Phase 0 (serial)
- **WP0 Bootstrap + probes**: vendor source (UPSTREAM.md), top-level CMake, `cmake/systems/Emscripten.cmake`, nlohmann json, DESKTOP/TESTS/TOOLS off, `graphics/core/device_factory.h`, `docs/manifest-format.md`, flags in **both** compile and link, NullDevice link smoke; **probe**: JSPI + `-fwasm-exceptions` + emdawnwebgpu triangle + JS-driven `await _frame()` loop + `TimedWaitAny`.

Phase 1 (parallel, disjoint)
- **WP1 Deps/platform**: PhysFS cross-compile, GL/GLEW/Intl/Gettext/SndFile CMake overrides, gettext stub, `system_emscripten.cpp` (GetSaveDir=/save), remove `lib/localename`, all `common/CMakeLists.txt` edits (reserve source-list variables for WP2/WP11).
- **WP2 Sound**: `vorbis_wav_reader` (stb_vorbis + WAV), `oalsound/buffer.cpp`, `resourcemanager.{h,cpp}` `GetSNDFileHandler` only, `alsound.{h,cpp}` CWorkerThread → inline executor, lazy decode on first Play.
- **WP3 Main loop/app**: `app.{h,cpp}` `Frame()` split + `extern "C" EMSCRIPTEN_KEEPALIVE int colobot_frame()`, remove SDL GL, window without `SDL_WINDOW_OPENGL`, no `SDL_Delay/WaitEvent`, `StartLoadingMusic` inline, resize → `m_device->Resize()`, `main.cpp` URL-query args; includes only `device_factory.h`.
- **WP4 WebGPU core**: `graphics/webgpu/` device (implements factory; owns instance/adapter/device/surface, `TimedWaitAny`), util, pipeline cache, ring buffer, frame/pass manager, texture, framebuffer, readback; **five stub renderers** with final names; shared WGSL `preamble/lighting/shadow` with frozen interface + clip-z fix; `engine.cpp:566` screenshot inline.
- **WP5 Asset tool**: as planned, writes manifest per `manifest-format.md`; WGSL not needed in data if embedded.
- **WP6 Web shell**: `index.html`, `loader.js` (WebGPU+JSPI detect, `await init()`, `await Module._main`, JS rAF loop awaiting `_colobot_frame`, audio unlock, progress), `sw.js`, `_headers`, `wrangler.jsonc`.
- **WP11 Lazy FS** (moved here): `web_archiver.c` (`PHYSFS_registerArchiver`; `stat/enumerate/exists` from manifest, `openRead` → `EM_ASYNC_JS` fetch), `boot.pak` `PHYSFS_mountMemory`, IDBFS `/save` + `idbfs_sync` (`EM_ASYNC_JS`), mounted from `pathman.cpp`/`system_emscripten.cpp` — **not** resourcemanager.

Phase 2 (parallel, after WP4; each only its own `wgpu_*_renderer.{h,cpp}` + own `*_vs/fs.wgsl`)
- **WP7 UI**, **WP8 Terrain** (`textureSampleCompare`, depthBias), **WP9 Object**, **WP10 Particle+Shadow**.
- **WP10b Shared WGSL reconcile** (serial, WP4 owner): fold requested changes into `preamble/lighting/shadow`.

Phase 3
- **WP12 Integration + Playwright** (Chromium with `--enable-unsafe-webgpu --enable-features=WebAssemblyExperimentalJSPI` only if the channel still needs it; menu screenshot; mission 1 via URL arg; save round-trip through IDBFS).
- **WP13 Fast-load**: WebP, chapter prefetch, i18n, `wasm-split` (note: `SPLIT_MODULE` under JSPI uses `loadSplitModule` async path, preamble.js L497/522 — supported), SW precache.

## 7. Verdict

The architecture (JSPI + emdawnwebgpu + SDL2 non-GL window + WebAudio OpenAL +
PhysFS lazy archiver) is sound and every toolchain primitive it relies on exists
in Emscripten 6.0.13 and the pinned emdawnwebgpu. Blocking defects are in the
plan's mechanics, not its direction: (1) link flags missing, (2) main-loop
re-entrancy under JSPI and unnamed frame export, (3) WP3/WP4 and WP2/WP11 and
WP4/WP7-10 file overlaps, (4) WP11 mis-phased (M1 has no assets without it).
Fix those in PLAN.md before starting Phase 1.
