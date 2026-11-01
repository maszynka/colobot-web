# Graphics analysis (sonnet)
- Code in upstream/colobot/colobot-base/src. Abstract Gfx::CDevice (graphics/core/device.h) + renderers (core/renderers.h: UI, Terrain, Object, Particle, Shadow). Only backend: graphics/opengl33 (4.6k LOC C++, 16 files) + 13 GLSL 330 shaders (812 lines) in opengl33/shaders/gl33, loaded at runtime via PhysFS.
- Factory Gfx::CreateDevice in opengl33/glutil.cpp:61, called app/app.cpp:647. GL leakage only in app/app.cpp (SDL GL context, swap). Engine (22k LOC) only uses CDevice.
- GL used: VAOs, streaming VBOs (glMapBufferRange), no index buffers, glDrawArrays with POINTS/LINES/LINE_STRIP/LINE_LOOP/TRIANGLES/STRIP/FAN; UBO in UI only; FBOs "shadow" (depth texture, compare mode, polygonOffset 2/8, 2048 atlas with 4 regions) and "multisample" (blit resolve); glReadPixels, glCopyTexSubImage, anisotropy, mipmaps, discard, blending modes.
- Textures: CImage (SDL2_image IMG_Load_RW) -> RGB/RGBA uncompressed; fonts SDL_ttf -> UpdateTexture subrects.
- WebGPU recs: graphics/webgpu/ with emdawnwebgpu; pipeline cache keyed on state; CPU-convert FAN/LOOP; ring buffers + dynamic offset uniforms; render-pass tracking per target; Depth32Float + comparison sampler, depthBias; MSAA 4 with resolveTarget; RGB->RGBA expand, CPU mipmaps; async readback; hand-port WGSL.
- Risks: async init/main loop, pass management, per-draw uniforms, readback/mipmaps.
