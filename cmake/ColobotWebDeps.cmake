# colobot-web: imported/interface targets replacing find_package() under Emscripten.
# Included from src/CMakeLists.txt when EMSCRIPTEN is set.
# Port flags themselves are added globally (compile AND link) by the root CMakeLists.txt.

set(COLOBOT_WEB_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

function(colobot_web_iface name)
    if(NOT TARGET ${name})
        string(REPLACE "::" "_" _lib "web_${name}")
        add_library(${_lib} INTERFACE)
        add_library(${name} ALIAS ${_lib})
    endif()
endfunction()

# SDL2 family, libpng, zlib: Emscripten ports (--use-port=...)
foreach(t SDL2::Core SDL2::Main SDL2::Image SDL2::TTF PNG::PNG ZLIB::ZLIB)
    colobot_web_iface(${t})
endforeach()

# SDL2 port headers live in <sysroot>/include/SDL2; upstream includes <SDL.h>
target_include_directories(web_SDL2_Core INTERFACE "${EMSCRIPTEN_SYSROOT}/include/SDL2")

# OpenGL / GLEW: not used on the web (graphics/opengl33 excluded). Empty targets.
colobot_web_iface(OpenGL::GL)
colobot_web_iface(GLEW::GLEW)

# OpenAL: Emscripten's built-in WebAudio-backed implementation (-lopenal at link)
colobot_web_iface(OpenAL::OpenAL)
target_link_options(web_OpenAL_OpenAL INTERFACE -lopenal)
target_include_directories(web_OpenAL_OpenAL INTERFACE "${EMSCRIPTEN_SYSROOT}/include/AL")

# gettext: identity stub (English only). Owner WP1; i18n in WP13.
colobot_web_iface(Intl::Intl)
target_include_directories(web_Intl_Intl INTERFACE "${COLOBOT_WEB_ROOT}/third_party/stubs/include")

# libsndfile: TEMPORARY stub so the link succeeds. TODO(WP2): replace with stb_vorbis/WAV reader.
if(NOT TARGET SndFile::sndfile)
    add_library(sndfile_stub STATIC "${COLOBOT_WEB_ROOT}/third_party/stubs/src/sndfile_stub.c")
    target_include_directories(sndfile_stub PUBLIC "${COLOBOT_WEB_ROOT}/third_party/stubs/include")
    add_library(SndFile::sndfile ALIAS sndfile_stub)
endif()

# glm: vendored headers (1.0.1)
colobot_web_iface(glm::glm)
target_include_directories(web_glm_glm SYSTEM INTERFACE "${COLOBOT_WEB_ROOT}/third_party/glm")

# nlohmann json: vendored single header (3.11.3), target defined in the root CMakeLists.txt

# PhysFS 3.2.0: cross-compiled from third_party/physfs (zip + dir archivers only).
# TODO(WP11): register the lazy web_archiver (PHYSFS_registerArchiver).
if(NOT TARGET PhysFS::PhysFS)
    set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(PHYSFS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    foreach(a 7Z GRP WAD CSM HOG MVL QPAK SLB ISO9660 VDF)
        set(PHYSFS_ARCHIVE_${a} OFF CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory("${COLOBOT_WEB_ROOT}/third_party/physfs" "${CMAKE_BINARY_DIR}/third_party/physfs" EXCLUDE_FROM_ALL)
    target_include_directories(physfs-static INTERFACE "${COLOBOT_WEB_ROOT}/third_party/physfs/src")
    add_library(PhysFS::PhysFS ALIAS physfs-static)
endif()
# PhysFS is plain C; upstream's -pedantic-errors (cmake/compilers/Clang.cmake) must not apply.
target_compile_options(physfs-static PRIVATE -Wno-pedantic -Wno-error)
