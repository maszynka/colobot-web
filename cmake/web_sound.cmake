# WP2 Sound (web only): stb_vorbis + vorbis_wav_reader, replacing libsndfile.
# Included from top-level CMakeLists.txt after add_subdirectory(src).
if(EMSCRIPTEN)
    add_library(stb_vorbis STATIC "${CMAKE_CURRENT_LIST_DIR}/../third_party/stb_vorbis/stb_vorbis.c")
    target_compile_definitions(stb_vorbis PRIVATE STB_VORBIS_NO_STDIO)
    target_compile_options(stb_vorbis PRIVATE -w)
    target_sources(Colobot-Base PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../src/colobot-base/src/common/resources/vorbis_wav_reader.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../src/colobot-base/src/common/resources/vorbis_wav_reader.h")
    target_link_libraries(Colobot-Base PUBLIC stb_vorbis)
endif()
