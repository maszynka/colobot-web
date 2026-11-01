##
# colobot-web top-level build
#   Web:    emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release && make -C build-web
#   Native: cmake -B build -DCMAKE_BUILD_TYPE=Release && make -C build   (needs system deps)
##
cmake_minimum_required(VERSION 3.21)

# third_party/physfs declares an old cmake_minimum_required
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# VERSION mirrors upstream: version.h.cmake uses CMAKE_PROJECT_VERSION (top-level project)
project(colobot_web VERSION 0.3.0 LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Upstream options (CMP0077: normal variables override option())
set(CBOT_STATIC ON)
set(DESKTOP OFF)
set(TESTS OFF)
set(TOOLS OFF)
set(PORTABLE ON)
set(PORTABLE_SAVES ON)
set(OPENAL_SOUND ON)

# nlohmann json: vendored single header, also used natively when the system has none
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND AND NOT TARGET nlohmann_json::nlohmann_json)
    add_library(colobot_web_json INTERFACE)
    target_include_directories(colobot_web_json SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    add_library(nlohmann_json::nlohmann_json ALIAS colobot_web_json)
endif()

if(EMSCRIPTEN)
    option(COLOBOT_WEB_LTO "Use -flto (slow link)" OFF) # TODO(WP13): turn ON for release builds

    # Flags that must be identical at compile AND link (PLAN-REVIEW §2 item 1)
    set(COLOBOT_WEB_COMMON_FLAGS
        -fwasm-exceptions
        --use-port=sdl2
        --use-port=sdl2_image:formats=png
        --use-port=sdl2_ttf
        --use-port=zlib
        --use-port=libpng
        # TODO(WP4): --use-port=emdawnwebgpu (never with -sUSE_WEBGPU=1)
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND COLOBOT_WEB_COMMON_FLAGS -O1 -g)
    else()
        list(APPEND COLOBOT_WEB_COMMON_FLAGS -O3)
    endif()
    if(COLOBOT_WEB_LTO)
        list(APPEND COLOBOT_WEB_COMMON_FLAGS -flto)
    endif()

    add_compile_options(${COLOBOT_WEB_COMMON_FLAGS})
    add_link_options(${COLOBOT_WEB_COMMON_FLAGS})
endif()

add_subdirectory(src)

if(EMSCRIPTEN)
    target_sources(colobot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/web/src/web_frame.cpp")

    # Main loop is driven from JS (await Module._colobot_frame()), not emscripten_set_main_loop.
    target_link_options(colobot PRIVATE
        -sJSPI
        "-sJSPI_EXPORTS=['main','colobot_frame']"
        "-sEXPORTED_FUNCTIONS=['_main','_colobot_frame']"
        "-sEXPORTED_RUNTIME_METHODS=['FS','callMain']"
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=256MB
        -sMAXIMUM_MEMORY=2GB
        -sSTACK_SIZE=1MB
        -sENVIRONMENT=web
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createColobot
        -sEXIT_RUNTIME=0
        -lidbfs.js
        # TODO(WP6/WP13): --closure 1 once the JS shell is settled
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(colobot PRIVATE -sASSERTIONS=2 -sSAFE_HEAP=1)
    endif()
    set_target_properties(colobot PROPERTIES SUFFIX ".js")
endif()
