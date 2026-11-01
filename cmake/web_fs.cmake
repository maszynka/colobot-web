# WP11 Lazy FS: web PhysFS archiver + IDBFS save sync (Emscripten only).
if(EMSCRIPTEN)
    target_sources(Colobot-Base PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../src/colobot-base/src/common/resources/web_archiver.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/colobot-base/src/common/resources/web_archiver.h"
    )
endif()
