// Shadow-map renderer WGSL - STUB (WP4). Owned by WP10.
// Port gl33/shadow_vs.glsl + shadow_fs.glsl (alpha-tested depth-only pass). Composed as: preamble + shadow_pass.
// Depth-only pipeline: no color target; target format Depth32Float; depthBias 8 / slope 2 (glPolygonOffset(2,8)).
@vertex
fn vs_main(@location(0) position: vec3f) -> @builtin(position) vec4f {
    return fix_clip(vec4f(position, 1.0));
}
