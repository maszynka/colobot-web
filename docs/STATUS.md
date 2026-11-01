# Status (WP12 / milestone M1) — 2026-11-02

**M1 reached:** the Release build boots to the main menu in real Chrome (WebGPU + JSPI), and mission 1 (`?runscene=missions101`) loads and renders terrain, objects, shadows and the HUD.

## How to reproduce

```sh
make -C build-web -j8                                  # wasm (see README)
node tools/build-data/build.mjs --out .data-out        # data, kept outside dist/
scripts/assemble-dist.sh build-web .data-out
node tests/e2e/boot.mjs --wait 11 --click              # menu: waits out welcome, clicks OK on player dialog
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

## Known issues / TODO

- The welcome screens (`CScreenWelcome`, 3 x 3 s) draw no splash image: the player dialog shows underneath, yet the first input during that window is swallowed by the welcome screen. Likely the background/over-color path of the UI renderer.
- No music: `build-data` was run without `--music`, so `music/Intro*.ogg` is missing and the engine retries every 3 s, which floods the log.
- With `runscene` and no existing player: `Unable to write ... savegame/Player/research.gam` (the directory was never created). This is harmless.
- Playwright reports `colobot.wasm`/`boot.pak` as `ERR_ABORTED`. The data does arrive: `loader.js` re-wraps the stream. Byte counts above come from file sizes.
- Chrome warns that the canvas format is not the preferred one (`bgra8unorm`), which costs an extra copy.
- Debug build variant not needed so far.
