target_sources(Colobot-Base PRIVATE
    config_file.cpp
    config_file.h
    error.h
    event.cpp
    event.h
    font_loader.cpp
    font_loader.h
    global.h
    image.cpp
    image.h
    ioutils.h
    key.cpp
    key.h
    language.cpp
    language.h
    logger.cpp
    logger.h
    profiler.cpp
    profiler.h
    regex_utils.cpp
    regex_utils.h
    restext.cpp
    restext.h
    settings.cpp
    settings.h
    singleton.h
    timeutils.h

    resources/inputstream.cpp
    resources/inputstream.h
    resources/inputstreambuffer.cpp
    resources/inputstreambuffer.h
    resources/outputstream.cpp
    resources/outputstream.h
    resources/outputstreambuffer.cpp
    resources/outputstreambuffer.h
    resources/physfs_utils.cpp
    resources/physfs_utils.h
    resources/resourcemanager.cpp
    resources/resourcemanager.h
    resources/sdl_file_wrapper.cpp
    resources/sdl_file_wrapper.h
    resources/sdl_memory_wrapper.cpp
    resources/sdl_memory_wrapper.h
    thread/worker_thread.h

    ${PLATFORM_SYSTEM_SOURCES}
)

# --- Web-only source hooks (owned by WP1; other packages just add the files) ---
# WP2 (sound): resources/vorbis_wav_reader.{h,cpp}. Once present it replaces sndfile_wrapper.
# WP11 (lazy FS): resources/web_archiver.{h,c}.
set(WEB_SOUND_SOURCES "")
set(WEB_FS_SOURCES "")
if(EMSCRIPTEN)
    foreach(f resources/vorbis_wav_reader.cpp resources/vorbis_wav_reader.h)
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${f}")
            list(APPEND WEB_SOUND_SOURCES ${f})
        endif()
    endforeach()
    foreach(f resources/web_archiver.c resources/web_archiver.h)
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${f}")
            list(APPEND WEB_FS_SOURCES ${f})
        endif()
    endforeach()
    target_sources(Colobot-Base PRIVATE ${WEB_SOUND_SOURCES} ${WEB_FS_SOURCES})
endif()

# libsndfile wrapper: native always; on web only until WP2's reader exists
if(NOT EMSCRIPTEN OR NOT WEB_SOUND_SOURCES)
    target_sources(Colobot-Base PRIVATE
        resources/sndfile_wrapper.cpp
        resources/sndfile_wrapper.h
    )
endif()

# Configure files
macro(SetFlag var name)
    if(${var})
        set(${name} "true")
    else()
        set(${name} "false")
    endif()
endmacro()

SetFlag(COLOBOT_OFFICIAL_BUILD CONFIG_OFFICIAL_BUILD)
SetFlag(COLOBOT_DEVELOPMENT_MODE CONFIG_DEVELOPMENT_BUILD)
SetFlag(PORTABLE_SAVES CONFIG_PORTABLE_SAVES)
SetFlag(USE_RELATIVE_PATHS CONFIG_RELATIVE_PATHS)

configure_file(config.h.cmake "${CMAKE_CURRENT_BINARY_DIR}/config.h")
configure_file(version.h.cmake "${CMAKE_CURRENT_BINARY_DIR}/version.h")

target_sources(Colobot-Base PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)
