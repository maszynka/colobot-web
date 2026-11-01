add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(model)
add_subdirectory(null)

# colobot-web: OpenGL 3.3 backend is native-only; the web build uses
# graphics/webgpu (WP4); graphics/null stays available as "null".
if(NOT EMSCRIPTEN)
    add_subdirectory(opengl33)
else()
    add_subdirectory(webgpu)
endif()
