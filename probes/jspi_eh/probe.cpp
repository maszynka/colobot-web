// WP0 probe: -fwasm-exceptions + -sJSPI together.
// Checks: (1) a JSPI-suspending import (EM_ASYNC_JS awaiting a JS Promise) works,
// (2) a C++ exception thrown AFTER resuming propagates through frames that were
// on the suspended stack and is caught, (3) an exception thrown inside a callee
// that itself suspended is caught by the caller, (4) repeated JS-driven frames
// via an exported function listed in JSPI_EXPORTS (mirrors colobot_frame).
#include <emscripten/emscripten.h>
#include <cstdio>
#include <stdexcept>
#include <string>

EM_ASYNC_JS(int, js_sleep_value, (int v), {
    await new Promise(r => setTimeout(r, 5));
    return v * 2;
});

struct Guard { const char* n; static int dtors; ~Guard() { ++dtors; } };
int Guard::dtors = 0;

static int deep(int depth, int v)
{
    Guard g{"deep"};
    if (depth == 0)
    {
        int r = js_sleep_value(v);           // suspends the whole wasm stack
        if (r == 42) throw std::runtime_error("thrown-after-resume");
        return r;
    }
    return deep(depth - 1, v);
}

static int g_frames = 0, g_ok = 0;

extern "C" EMSCRIPTEN_KEEPALIVE int probe_frame()
{
    ++g_frames;
    try
    {
        deep(8, 21);                          // 21*2 == 42 -> throws
        std::printf("FAIL frame %d: no exception\n", g_frames);
    }
    catch (const std::runtime_error& e)
    {
        if (std::string(e.what()) == "thrown-after-resume") ++g_ok;
    }
    return g_frames < 3;                      // ask JS for 3 frames total
}

int main()
{
    int ok = 0;
    try { deep(4, 21); }
    catch (const std::exception& e) { ok = std::string(e.what()) == "thrown-after-resume"; }
    int plain = deep(2, 5);                   // no throw: returns 10
    std::printf("main: caught=%d plain=%d dtors=%d\n", ok, plain, Guard::dtors);
    std::printf("RESULT_MAIN %s\n", (ok && plain == 10 && Guard::dtors == 5 + 3) ? "PASS" : "FAIL");
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int probe_summary()
{
    std::printf("frames=%d ok=%d dtors=%d\n", g_frames, g_ok, Guard::dtors);
    return g_ok == g_frames && g_frames == 3;
}
