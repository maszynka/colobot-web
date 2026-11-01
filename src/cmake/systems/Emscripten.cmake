# colobot-web: Emscripten (wasm32) target. Based on Other.cmake.
message(STATUS "Build for Emscripten (web)")

set(PLATFORM_GNU     0)
set(PLATFORM_MACOSX  0)
set(PLATFORM_FREEBSD 0)
set(PLATFORM_EMSCRIPTEN 1)

set(PLATFORM_INSTALL_BIN_DIR ".")
set(PLATFORM_INSTALL_LIB_DIR ".")
set(PLATFORM_INSTALL_DATA_DIR "data")
set(PLATFORM_INSTALL_I18N_DIR "lang")
set(PLATFORM_INSTALL_DOC_DIR "doc")

set(PLATFORM_SYSTEM_SOURCES
    system/system.cpp
    system/system.h

    system/system_emscripten.cpp
    system/system_emscripten.h

    CACHE INTERNAL "System sources"
)
