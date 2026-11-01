// UI renderer (WP4 reference implementation; WP7 owns further changes). Port of gl33/ui_*.glsl.
struct UIUniforms {
    matrix: mat4x4f,
    color: vec4f,
};
@group(1) @binding(0) var<uniform> u_ui: UIUniforms;
@group(2) @binding(0) var t_texture: texture_2d<f32>;
@group(2) @binding(1) var s_texture: sampler;

struct VsOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};

@vertex
fn vs_main(@location(0) position: vec2f, @location(1) uv: vec2f, @location(2) color: vec4f) -> VsOut {
    var o: VsOut;
    o.position = fix_clip(u_ui.matrix * vec4f(position, 0.0, 1.0));
    o.color = color;
    o.uv = uv;
    return o;
}

@fragment
fn fs_main(v: VsOut) -> @location(0) vec4f {
    // gl33 ui_fs.glsl ignores uni_Color too; kept identical.
    return v.color * textureSample(t_texture, s_texture, v.uv);
}
