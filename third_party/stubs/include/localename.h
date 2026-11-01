/* colobot-web: replacement for gnulib lib/localename (not built under Emscripten).
 * gl_locale_name() reports navigator.language as a POSIX-style name ("pl_PL"). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Implemented in common/system/system_emscripten.cpp */
const char* colobot_web_locale_name(void);
static inline const char* gl_locale_name(int category, const char* categoryname)
{
    (void)category; (void)categoryname;
    return colobot_web_locale_name();
}
#ifdef __cplusplus
}
#endif
