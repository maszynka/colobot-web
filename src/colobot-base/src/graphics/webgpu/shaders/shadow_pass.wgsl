// Shadow-map pass WGSL (WP10). Port of gl33/shadow_vs.glsl + shadow_fs.glsl. Composed as: preamble + shadow_pass.
// Depth-only. vs_main is shared; fs_alpha is the alpha-tested variant (zero color targets).
struct ShadowPassUniforms {
    mvp: mat4x4f,       // projection * view(z flipped) * model
};
@group(1) @binding(0) var<uniform> u_pass: ShadowPassUniforms;
@group(2) @binding(0) var t_texture: texture_2d<f32>;
@group(2) @binding(1) var s_texture: sampler;

struct VsOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@vertex
fn vs_main(@location(0) position: vec3f, @location(1) uv: vec2f) -> VsOut {
    var o: VsOut;
    o.position = fix_clip(u_pass.mvp * vec4f(position, 1.0));
    o.uv = uv;
    return o;
}

@fragment
fn fs_alpha(v: VsOut) {
    let alpha = textureSample(t_texture, s_texture, v.uv).a;
    if (alpha < 0.5) { discard; }
}
