// Particle renderer WGSL - STUB (WP4). Owned by WP10.
// Port gl33/particle_vs.glsl + particle_fs.glsl. Composed as: preamble + shadow + particle.
// See docs/webgpu-renderer-guide.md (group 0 shared scene, group 1 own uniforms, group 2 own textures).
@vertex
fn vs_main(@location(0) position: vec3f) -> @builtin(position) vec4f {
    return fix_clip(vec4f(position, 1.0));
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return vec4f(1.0, 0.0, 1.0, 1.0);
}
