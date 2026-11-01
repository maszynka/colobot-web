# WebGPU renderer guide (WP4 → WP7–WP10)

Backend: `src/colobot-base/src/graphics/webgpu/`. The device (WP4) owns everything shared;
each Phase 2 package edits **only** its own files:

| Package | Files |
|---|---|
| WP7 UI | `wgpu_ui_renderer.{h,cpp}`, `shaders/ui.wgsl` (already fully implemented, the reference) |
| WP8 Terrain | `wgpu_terrain_renderer.{h,cpp}`, `shaders/terrain.wgsl` |
| WP9 Object | `wgpu_object_renderer.{h,cpp}`, `shaders/object.wgsl` |
| WP10 Particle + Shadow | `wgpu_particle_renderer.{h,cpp}`, `shaders/particle.wgsl`, `wgpu_shadow_renderer.{h,cpp}`, `shaders/shadow_pass.wgsl` |

Frozen (change requests go to WP10b, the WP4 owner): `shaders/preamble.wgsl`, `shaders/shadow.wgsl`,
`shaders/lighting.wgsl`, `wgpu_scene.{h,cpp}`, the device, pass manager, ring buffers, pipeline cache, texture manager.

Reference GL implementation: `graphics/opengl33/gl33_*_renderer.cpp` and `opengl33/shaders/gl33/*.glsl`.

## WGSL

* Every `shaders/*.wgsl` is embedded at configure time as `Gfx::WGSL::<basename>` in the generated
  `wgsl_sources.h` (`#include "wgsl_sources.h"`). Adding/editing a file re-runs CMake automatically.
* No `#include` in WGSL: modules are concatenated with `CreateShaderModule(device, label, {parts...})`.
  Stubs already use the right composition:
  * terrain/object: `preamble + shadow + lighting + <own>`
  * particle: `preamble + shadow + particle`
  * shadow pass: `preamble + shadow_pass`
* Entry points: `vs_main`, `fs_main`.
* **Clip-z fix**: the engine produces GL matrices (z in [-w, w]). Every vertex stage must output
  `fix_clip(clip_position)`. Do not change projection matrices in C++.
* **Render-target V flip**: textures rendered by WebGPU (shadow map, `CopyFramebufferToTexture` targets,
  framebuffer colour textures) have row 0 at the top. Sample them with `flip_rt_uv(uv)` when the UVs
  come from GL conventions. `calculate_shadow()` already does this.
* Use `textureSampleCompareLevel` (not `textureSampleCompare`) inside non-uniform control flow.

## Bind groups (frozen convention)

| Group | Content | Who |
|---|---|---|
| 0 | scene: `@binding(0)` `LightUniforms`, `@binding(1)` `ShadowUniforms` (both dynamic offsets), `@binding(2)` shadow map `texture_depth_2d`, `@binding(3)` `sampler_comparison` | `CWebGPUSceneBindings` (`wgpu_scene.h`), layout `device->SceneBindGroupLayout()`; UI/shadow pass bind `device->EmptyBindGroup()` with `EmptyBindGroupLayout()` |
| 1 | renderer-local uniforms, `hasDynamicOffset = true`, ring-allocated | renderer |
| 2 | renderer textures + samplers | renderer |

`CWebGPUSceneBindings` (already a member `m_scene` in terrain/object stubs): the stubs already forward
`SetLight/SetSky/SetLighting/SetShadowParams/SetShadowMap` into it; call `m_scene.Bind(*pass)` before drawing.
Particles that need shadows add their own `CWebGPUSceneBindings` member in their own header.

## Per-draw pattern (copy from `CWebGPUUIRenderer::EndPrimitive`)

```cpp
wgpu::RenderPassEncoder* pass = m_device->Passes().GetPass();   // lazily opens the pass
if (pass == nullptr) return;                                     // outside BeginScene/EndScene

// 1. vertices: static buffers -> CWebGPUVertexBuffer (GetGPUBuffer/GetGPUVertexCount/GetGPUTopology;
//    FAN/LOOP/POINTS already converted). Streamed vertices -> ConvertPrimitives() when
//    NativeTopology(type) == Undefined, then m_device->VertexRing().Upload(data, bytes).
// 2. pipeline: key from current device state + current target (formats, MSAA, blend, depth, cull)
PipelineKey key = m_device->MakePipelineKey(RendererId::Terrain, topology);
key.variant = ...;                       // own shader variants (e.g. alpha-scissor on/off)
const auto& pipeline = m_device->Pipelines().Get(key, [this](const PipelineKey& k) { return CreatePipeline(k); });
// CreatePipeline: set layout/vertex/entry points; call m_device->FillFixedFunction(k, desc, ff)
// for blend/depth/cull/targets/multisample; then desc.fragment = &ff.fragment (if k.colorFormat != 0).
// 3. uniforms: RingAllocation a = m_device->UniformRing().Upload(&u, sizeof(u));
//    bind group for group(1) is keyed on a.buffer (recreate when the ring chunk changes),
//    pass->SetBindGroup(1, group, 1, &offset) with offset = a.offset (256-aligned).
// 4. textures: m_device->Textures().Get(id) -> view + sampler (id 0 / unknown -> 1x1 white);
//    cache bind groups by entry.serial; clear the cache when Textures().Epoch() changes.
pass->SetPipeline(pipeline);
m_scene.Bind(*pass);                     // group 0 (3D renderers)
...
pass->SetVertexBuffer(0, buffer, offset, size);
pass->Draw(count, 1, first, 0);
```

State notes:
* Global state (`SetDepthTest/SetDepthMask/SetCullFace/SetTransparency/SetColorMask`) lives in the device
  and is captured into `PipelineKey` by `MakePipelineKey`. Renderers that expose these setters forward them
  (object stub already does).
* Front face is CW (Colobot's reversed winding, same as gl33). `CullFace::BOTH` maps to None in WebGPU — skip the draw yourself if needed.
* Depth: `Depth32Float` everywhere, compare `LessEqual` when the test is on, writes only when test **and** mask are on.
* Shadow pass (WP10): `CWebGPUShadowRenderer::Begin/End` already switch the pass target to the shadow map
  (depth-only, cleared) and back; `SetShadowRegion` sets the atlas viewport. Pipelines there have
  `key.colorFormat == 0` (no fragment targets; alpha-tested variant needs a fragment stage with zero targets
  that uses `discard`), `key.depthBias = 8`, `key.depthBiasSlopeScale = 2.0f` (gl33 `glPolygonOffset(2, 8)`; tune).
* Never cache a `RenderPassEncoder*` across calls — target changes, clears and copies end the pass.

## Device facts

* Instance with `TimedWaitAny`; adapter/device requested with `CallbackMode::WaitAnyOnly` + `instance.WaitAny`
  (suspends through JSPI, so `Create()` must run inside a JSPI export: `main`/`colobot_frame`).
* Surface: canvas `#canvas`, format `RGBA8Unorm`, usage RenderAttachment|CopySrc|CopyDst, no `Present()` on the web.
* Frame: `BeginScene` acquires the swapchain texture, creates the encoder, clears; `EndScene` flushes the
  ring buffers (one `WriteBuffer` per chunk) and submits once.
* Framebuffers: `"default"` = swapchain; `"multisample"` = 4x MSAA colour+depth, `CopyToScreen` resolves with a
  `resolveTarget` pass (full-size only); `"shadow"` = Depth32Float depth-only, comparison sampler.
  `CreateDepthTexture()` is what the engine actually uses for the shadow map (also Depth32Float + comparison sampler).
* `GetFrameBufferPixels()` is a stub (black image) — TODO(WP12): CopyTextureToBuffer + MapAsync/WaitAny.
