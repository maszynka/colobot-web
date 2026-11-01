// colobot-web shared PBR lighting (WP4, FROZEN INTERFACE). Port of gl33/lighting.glsl.
@group(0) @binding(0) var<uniform> u_light: LightUniforms;

fn schlick_fresnel(LdH: f32, metalness: f32, color: vec3f) -> vec3f {
    let f = mix(vec3f(0.04), color, metalness);
    return f + (1.0 - f) * pow(1.0 - LdH, 5.0);
}

fn geometry_smith(dot_prod: f32, roughness: f32) -> f32 {
    let k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    let denom = dot_prod * (1.0 - k) + k;
    return 1.0 / denom;
}

fn ggx_distribution(NdH: f32, roughness: f32) -> f32 {
    let alpha2 = roughness * roughness * roughness * roughness;
    let d = (NdH * NdH) * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * d * d);
}

fn calculate_lighting(position: vec3f, normal: vec3f, albedo: vec3f, emissive: vec3f,
                      shadow: f32, ambient_occlusion: f32, roughness: f32, metalness: f32) -> vec3f {
    let diffuse_brdf = mix(albedo, vec3f(0.0), metalness);
    let light = normalize(u_light.light_position.xyz);
    let view = normalize(u_light.camera_position - position);
    let halfway = normalize(view + light);
    let NdH = dot(normal, halfway);
    let LdH = dot(light, halfway);
    let NdL = max(dot(normal, light), 0.0);
    let NdV = dot(normal, view);
    let spec_brdf = 0.25 * ggx_distribution(NdH, roughness) * schlick_fresnel(LdH, metalness, albedo)
                  * geometry_smith(NdL, roughness) * geometry_smith(NdV, roughness);
    let diffuse_specular = (diffuse_brdf + PI * spec_brdf) * u_light.light_intensity * u_light.light_color * NdL * shadow;
    let ambient = albedo * u_light.sky_color * u_light.sky_intensity * ambient_occlusion;
    return ambient + emissive + diffuse_specular;
}
