# colobot-web

Colobot: Gold Edition ported to the browser (Emscripten + WebGPU + JSPI). Plan: `docs/PLAN.md`, `docs/PLAN-REVIEW.md`.

Build (web): `emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release && make -C build-web -j` -> `build-web/src/colobot.{js,wasm}`.
First build: pre-warm ports with `embuilder build sdl2 sdl2_image:formats=png sdl2_ttf zlib libpng freetype harfbuzz` (parallel make races on the port cache lock).
