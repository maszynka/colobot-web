# Status (WP12 / milestone M1) — 2026-11-02

**M1 reached:** the Release build boots to the main menu in real Chrome (WebGPU + JSPI), and mission 1 (`?runscene=missions101`) loads and renders terrain, objects, shadows and the HUD.

## How to reproduce

```sh
make -C build-web -j8                                  # wasm (see README)
node tools/build-data/build.mjs --out .data-out --music  # data (needs oxipng, oggenc), kept outside dist/
scripts/assemble-dist.sh build-web .data-out
node tests/e2e/boot.mjs --wait 4 --click               # menu: clicks OK on player dialog (welcome skipped on web)
node tests/e2e/boot.mjs --wait 6 --click --throttle 50  # same over emulated 50 Mbit/s (CDP)
node tests/e2e/boot.mjs --wait 25 --query runscene=missions101
```

`tests/e2e/boot.mjs` serves `dist/` on `localhost` with the correct wasm MIME type. It launches system Chrome (`channel: 'chrome'`, `--enable-unsafe-webgpu`) **headless**, where WebGPU and JSPI are both available on macOS. It writes `console.log`, `network.json`, `summary.json` and screenshots to `tests/e2e/out/` (gitignored). Other flags are `--headed`, `--query QS` (for example `loglevel=debug&debug=sys_events`) and `--steps 'x,y,ms;...'` (scripted clicks).

## Screenshots

- `docs/screenshots/first-paint.png`: first frame (player-name dialog)
- `docs/screenshots/main-menu.png`: main menu after you pick a player
- `docs/screenshots/mission101.png`: mission 1, chapter 1

## Timings (headless Chrome, localhost, M-series Mac, 1280x800)

| metric | value |
| --- | --- |
| loading overlay hidden (first app frame) | ~1.1 s |
| 60 app frames rendered | ~2.3 s |
| main menu interactive | after the 3x3 s welcome sequence (~9.5 s), or immediately in mission mode |
| bytes to menu | ~28.6 MB raw: wasm 4.6 MB (1.6 MB gzip), boot.pak 9.3 MB, manifest 0.24 MB, intro.pak 7.0 MB, sounds.pak 7.2 MB (fetched lazily at ~9.6 s) |
| bytes to mission 1 | ~45 MB raw: adds lvl-missions-chapter001, common, models packs |

## Verified

- WebGPU adapter and JSPI (`WebAssembly.Suspending`) are present in headless Chrome.
- **Lazy fetch works.** Only `boot.pak` and the manifest load up front. `intro.pak`, `sounds.pak`, the level pack, `common.pak` and `models.pak` load on demand (see `network.json`).
- `/save` is writable: an FS write/read/unlink probe succeeds, and creating a player profile works.
- Audio: OpenAL opens and sounds load with no crash. `Module.AL` is not exposed, so the harness cannot read AudioContext state.
- Mouse and keyboard input work. Hover, tooltips and button clicks all work.
- **Resize loop fixed.** Previously `loader.js` and SDL's resizable-window handler both wrote `canvas.width/height`, so the resolution jumped. Now JS is the single owner (CSS 100vw/100vh, backing = CSS x dpr, `resize` + `ResizeObserver`). On web the SDL window is not resizable. `CApplication::SyncCanvasSize()` reads the canvas size each frame and reconfigures the device, and `ChangeVideoConfig` ignores requested sizes on web. Tested at dpr 2 with mid-run viewport changes: 2560x1600 -> 2000x1400, stable.

## WP13 / M3 — load speed (2026-11-02)

Harness (`tests/e2e/boot.mjs`) now counts bytes the server actually sent (`served[]`, wire bytes),
emulates Cloudflare edge brotli for js/json/wasm (`--no-br` to disable), polls every 100 ms,
supports `--throttle MBPS` (CDP `Network.emulateNetworkConditions`, 20 ms RTT) and reports
`longFrames` (frames > 50 ms).

| metric | before | after |
| --- | --- | --- |
| first frame, localhost | ~1.06 s (1 s poll) | 0.51 s |
| first frame, 50 Mbit/s | 3.08 s (no br) | 1.54 s (no br), 1.20 s (br) |
| main menu interactive | ~9.5 s (3x3 s welcome) + click | at first frame (player dialog) + click |
| bytes before first frame | 14.2 MB raw (wasm 4.6, boot.pak 9.3) | 5.8 MB wire (wasm 3.68 MB / 1.25 MB br, boot.pak.gz 2.94 MB, manifest 75 KB br) |
| bytes to main menu | 28.8 MB (intro.pak 7.0, sounds.pak 7.2 MB) | ~19 MB, of which 8.5 MB is menu music fetched in the background + low-priority prefetch of models/common (6.4 MB); sounds per file (4–20 KB each) |
| mission 1 total | ~45 MB | 14.2 MB; first frame 2.09 s at 50 Mbit/s |
| wasm | 4.60 MB (O3) | 3.68 MB (-Oz); 60 fps in mission 1 either way |

What changed:

- **Data layout** (`build.mjs`): `boot.pak` 9.3 -> 4.05 MB (`generico.png` 2.5 MB is the quit/credits screen only, `icons/` 2.3 MB are SatCom help images; `*.svg` dropped), plus `boot.pak.gz` 2.94 MB that `loader.js` inflates with `DecompressionStream` (Cloudflare does not compress `application/octet-stream`). Sounds, intro, icons, music and any non-boot file >= 64 KiB are loose files; small files stay in packs. PNGs via `oxipng -o 3` (lossless). Sounds are Ogg Vorbis inside `.wav` names (`vorbis_wav_reader` sniffs `RIFF`/`OggS`): 7.2 -> 1.1 MB; `--sounds-wav` turns it off.
- **Welcome screens skipped on web** (`CController::StartApp`).
- **Music**: built with `--music` (loose, 21 files). `CALSound` never blocks a frame on music: if the bytes are not resident it starts a background fetch (`web_fs_ready`) and plays from `FrameMove`; files missing from the manifest are skipped silently (no 3 s retry spam). Intro2 is fetched only after Intro1.
- **Archiver**: one in-flight fetch per pack/file, `prefetch()` with `priority: 'low'`; `loader.js` prefetches `models` and `common` ~3 s after the first frame (skipped with Save-Data). `sw.js` caches by manifest sha256 (incl. `boot.pak.gz`), revalidates misses (`cache: 'no-cache'`) and never blocks a data request on the manifest.
- **Preload hints** in `index.html`: downloads start at ~35 ms instead of ~60 ms.
- **wasm streaming**: the native `Response` goes to `instantiateStreaming` (progress read from a clone), so streaming compile and the code cache work again (this was the `ERR_ABORTED`).
- **Canvas format**: surface uses the preferred format (bgra8unorm, first in surface capabilities); offscreen color framebuffers follow it, so the per-frame conversion copy and Chrome warning are gone.
- **Bug fixed**: `wgpu::RenderPassDescriptor::colorAttachmentCount` has no default initializer in `webgpu_cpp.h`; depth-only (shadow) passes sent garbage. It happened to be 0 at -O3 and crashed mission 1 at -Oz and with LTO.

Compiler profiles measured (wasm / brotli-5): -O3 4.60 / 1.43 MB; -Oz 3.68 / 1.25 MB; LTO -O3 5.32 / 1.52 MB; LTO -Os 4.28 / 1.37 MB; LTO -Oz 3.80 / 1.27 MB. Default is now -Oz without LTO (`COLOBOT_WEB_OPT`, `COLOBOT_WEB_LTO`). `--closure 1` (`COLOBOT_WEB_CLOSURE`) fails with a Closure internal error on the ES-module output (`JSC_ILLEGAL_MODULE_RENAMING_CONFLICT`, Java 21 required) and stays off; the JS glue is 57 KB br anyway. wasm-opt already runs inside emcc at -O2+.

Brotli: Cloudflare Workers static assets do not serve precompressed `.br` siblings; the edge compresses js/json/wasm itself, so no `.br` files ship. Only `boot.pak` needs help (gzip twin above).

Pipeline prewarm: not needed for now. Mission 1 has no frame > 50 ms. The menu has one ~330 ms frame right after the first frame (texture loading in `LoadAllTextures`, not pipelines).

### Gemini 3.8 Flash suggestions (`docs/optimization-suggestions-gemini.md`)

| # | item | status |
| --- | --- | --- |
| 1 | skip welcome sequence | implemented: menu reachable ~9 s earlier |
| 2 | lean boot.pak | implemented: 9.3 -> 4.05 MB (2.94 MB gz). Level previews are tiny (78 KB) and stay |
| 3 | precompressed brotli | partly: CF serves no `.br` siblings and compresses js/json/wasm at the edge; boot.pak gets a gzip twin (-1.3 MB) |
| 4 | preload hints | implemented: critical fetches start ~25 ms earlier |
| 5 | SW blocks data on manifest | partly valid: first visit is not SW-controlled, but a cold SW did await the manifest. Now races and falls through to the network |
| 6 | -Oz / LTO / closure | -Oz implemented (-0.92 MB, -0.18 MB br). LTO rejected (larger in every profile). Closure blocked (compiler error) |
| 7 | preferred canvas format | implemented |
| 8 | GPU mipmaps | deferred: mission 1 shows no long frames; load-time win not measured yet |
| 9 | pipeline prewarm | deferred: no shader stalls visible (see above) |
| 10 | keep big level files in packs | rejected: mission 1 fetches only 6 loose files (all issued by ~1.45 s); no serial waterfall seen |
| 11 | native Response for instantiateStreaming | implemented (valid: the JS-wrapped stream blocked streaming compile and the code cache) |
| 12 | WebP textures | rejected for now: the emscripten `sdl2_image` port has no libwebp, so `formats=webp` cannot build. oxipng (lossless) is used instead |
| 13 | ring buffer overwrites in-flight data | rejected: claim is wrong. The ring has a CPU staging copy flushed once per frame with `queue.WriteBuffer`, which is queue-ordered (no in-flight hazard), and it already writes only `used` bytes |
| 14 | redundant SetPipeline/SetBindGroup | deferred: per-frame CPU, not load time; mission runs at 60 fps |
| 15 | wasm-split | deferred: high risk, wasm already 1.25 MB br |

## Accounts + cloud saves (2026-11-02)

The Worker now has `main` (`worker/index.js`) for `/api/*` only; static files are still served directly from assets. The web shell offers Google, email + password, or local play. `web_archiver.c` hooks (`Module.onSaveMounted`, `Module.onSaveFlushed`) let `web/account.js` restore `/save` from the cloud before the game reads it, and upload a zip 5 s after each flush. `tests/e2e/accounts.mjs` passes 11/11 checks against `wrangler dev`, and `tests/e2e/save-roundtrip.mjs` still passes. The remote D1 migration and the Google secrets are still pending (`docs/ACCOUNTS.md`).

## Known issues / TODO

- Welcome screens are skipped on web. On desktop builds they still draw no splash image (likely the background/over-color path of the UI renderer).
- With `runscene` and no existing player: `Unable to write ... savegame/Player/research.gam` (the directory was never created). This is harmless.
- Data names are not content-addressed; `/data/*` is `immutable` in `_headers`. The SW revalidates on a hash miss, but a client without the SW could see a stale pack after a deploy. Hashed URLs would fix it.
- Debug build variant not needed so far.
