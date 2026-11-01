// colobot-web shared WGSL preamble (WP4, FROZEN INTERFACE - see docs/webgpu-renderer-guide.md)
// Concatenated in front of every renderer shader: preamble + [shadow] + [lighting] + <renderer>.wgsl
//
// Bind group convention (all renderers):
//   group(0) = shared scene  : binding 0 LightUniforms, 1 ShadowUniforms, 2 shadow map, 3 comparison sampler
//   group(1) = renderer-local uniforms (dynamic offsets, 256-byte aligned, ring-allocated per draw)
//   group(2) = renderer textures/samplers
// The UI renderer binds the device's empty group(0).

const PI: f32 = 3.1415926;

// GL clip space has z in [-w, w]; WebGPU expects [0, w].
// Every vertex stage MUST pass its final clip position through fix_clip().
fn fix_clip(p: vec4f) -> vec4f {
    return vec4f(p.x, p.y, (p.z + p.w) * 0.5, p.w);
}

// Render targets written by WebGPU have row 0 at the TOP, GL ones at the bottom.
// Use when sampling a render target (shadow map, copied framebuffer) with GL-convention UVs.
fn flip_rt_uv(uv: vec2f) -> vec2f {
    return vec2f(uv.x, 1.0 - uv.y);
}

struct ShadowParam {
    transform: mat4x4f,
    uv_offset: vec2f,
    uv_scale: vec2f,
};  // 80 bytes

struct ShadowUniforms {
    params: array<ShadowParam, 4>,
    regions: i32,
    _pad0: i32,
    _pad1: i32,
    _pad2: i32,
};  // 336 bytes

struct LightUniforms {
    camera_position: vec3f,
    light_intensity: f32,
    light_position: vec4f,
    light_color: vec3f,
    sky_intensity: f32,
    sky_color: vec3f,
    lighting: u32,          // 0 = lighting disabled
};  // 64 bytes
