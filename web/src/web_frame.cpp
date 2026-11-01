// colobot-web: JS-driven main loop entry point (PLAN-REVIEW §1, item 2).
// The loader drives frames with:
//   async function tick() { if (await Module._colobot_frame()) requestAnimationFrame(tick); }
// colobot_frame is listed in -sJSPI_EXPORTS so it may suspend on JSPI imports.
// main() creates and initializes the (static) CApplication, calls BeginRun()
// and returns without looping; each call here runs one CApplication::Frame().

#include "app/app.h"

#include "common/logger.h"

#include <emscripten/emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE int colobot_frame()
{
    static bool finished = false;
    if (finished)
        return 0;

    CApplication* app = CApplication::GetInstancePointer();
    if (app == nullptr)
        return 0; // main() failed or has not run yet

    if (!app->Frame())
    {
        finished = true;
        GetLogger()->Info("Exiting with code %%", app->GetExitCode());
        return 0; // 0 = stop requesting frames
    }
    return 1;
}
