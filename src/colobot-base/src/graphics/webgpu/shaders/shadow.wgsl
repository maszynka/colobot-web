// colobot-web shared shadow sampling (WP4, FROZEN INTERFACE). Port of gl33/shadow.glsl.
@group(0) @binding(1) var<uniform> u_shadow: ShadowUniforms;
@group(0) @binding(2) var t_shadow_map: texture_depth_2d;
@group(0) @binding(3) var s_shadow_map: sampler_comparison;

fn project_shadows(position: vec3f) -> array<vec3f, 4> {
    var result: array<vec3f, 4>;
    for (var i: i32 = 0; i < u_shadow.regions; i++) {
        let projected = u_shadow.params[i].transform * vec4f(position, 1.0);
        result[i] = projected.xyz / projected.w;
    }
    return result;
}

// textureSampleCompareLevel has no uniform-control-flow requirement, so it is legal inside the loop.
fn calculate_shadow(projected: array<vec3f, 4>) -> f32 {
    for (var i: i32 = 0; i < u_shadow.regions; i++) {
        let p = projected[i];
        if (p.x < 0.0 || p.x > 1.0) { continue; }
        if (p.y < 0.0 || p.y > 1.0) { continue; }
        let uv = p.xy * u_shadow.params[i].uv_scale + u_shadow.params[i].uv_offset;
        // the shadow map is a WebGPU render target -> flip V
        return textureSampleCompareLevel(t_shadow_map, s_shadow_map, flip_rt_uv(uv), p.z);
    }
    return 1.0;
}
