// Particle renderer WGSL (WP10). Port of gl33/particle_vs.glsl + particle_fs.glsl.
// Composed as: preamble + shadow + particle. group(0) is unused (device empty group).
struct ParticleUniforms {
    mvp: mat4x4f,       // projection * view(z flipped) * model, computed on the CPU
    color: vec4f,
};
@group(1) @binding(0) var<uniform> u_particle: ParticleUniforms;
@group(2) @binding(0) var t_texture: texture_2d<f32>;
@group(2) @binding(1) var s_texture: sampler;

const ALPHA_SCISSOR: f32 = 0.5;

struct VsOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};

@vertex
fn vs_main(@location(0) position: vec3f, @location(1) color: vec4f, @location(2) uv: vec2f) -> VsOut {
    var o: VsOut;
    o.position = fix_clip(u_particle.mvp * vec4f(position, 1.0));
    o.color = color * u_particle.color;
    o.uv = uv;
    return o;
}

@fragment
fn fs_main(v: VsOut) -> @location(0) vec4f {
    let tex = textureSample(t_texture, s_texture, v.uv);
    if (tex.a < ALPHA_SCISSOR) { discard; }
    return v.color * tex;
}
