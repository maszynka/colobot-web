# colobot-web

Colobot: Gold Edition ported to the browser (Emscripten + WebGPU + JSPI). Plan: `docs/PLAN.md`, `docs/PLAN-REVIEW.md`.

Build (web): `emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release && make -C build-web -j` -> `build-web/src/colobot.{js,wasm}`.
First build: pre-warm ports with `embuilder build sdl2 sdl2_image:formats=png sdl2_ttf zlib libpng freetype harfbuzz` (parallel make races on the port cache lock).

## License

GPL-3.0, same as upstream [Colobot](https://github.com/colobot/colobot) and [colobot-data](https://github.com/colobot/colobot-data) (see `LICENSE`). Game assets are not stored in this repo; they are built from colobot-data by `npm run build-data`.
