// colobot-web: JS-driven main loop entry point (PLAN-REVIEW §1, item 2).
// The loader drives frames with:
//   async function tick() { if (await Module._colobot_frame()) requestAnimationFrame(tick); }
// colobot_frame is listed in -sJSPI_EXPORTS so it may suspend on JSPI imports.
// TODO(WP3): call CApplication::Frame() here (Run() split into init + Frame()).

#include <emscripten/emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE int colobot_frame()
{
    return 0; // 0 = stop requesting frames (placeholder)
}
