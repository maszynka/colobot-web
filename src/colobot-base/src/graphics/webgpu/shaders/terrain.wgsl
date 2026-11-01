// Terrain renderer WGSL (WP8). Port of gl33/terrain_vs.glsl + terrain_fs.glsl.
// Composed as: preamble + shadow + lighting + terrain.
// group(0) shared scene, group(1) TerrainUniforms (dynamic offset), group(2) textures/samplers.

struct TerrainUniforms {
    projection: mat4x4f,
    view: mat4x4f,
    model: mat4x4f,
    normal_matrix: mat4x4f,     // upper-left 3x3 is the normal matrix
    albedo_color: vec4f,
    emissive_color: vec3f,
    roughness: f32,
    fog_color: vec3f,
    metalness: f32,
    fog_range: vec2f,
    ao_strength: f32,
    _pad: f32,
};  // 320 bytes

@group(1) @binding(0) var<uniform> u_terrain: TerrainUniforms;

@group(2) @binding(0) var t_albedo: texture_2d<f32>;
@group(2) @binding(1) var t_detail: texture_2d<f32>;
@group(2) @binding(2) var t_emissive: texture_2d<f32>;
@group(2) @binding(3) var t_material: texture_2d<f32>;
@group(2) @binding(4) var s_albedo: sampler;
@group(2) @binding(5) var s_detail: sampler;
@group(2) @binding(6) var s_emissive: sampler;
@group(2) @binding(7) var s_material: sampler;

struct VsIn {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv0: vec2f,
    @location(4) uv1: vec2f,
};

struct VsOut {
    @builtin(position) clip: vec4f,
    @location(0) color: vec4f,
    @location(1) uv0: vec2f,
    @location(2) uv1: vec2f,
    @location(3) normal: vec3f,
    @location(4) position: vec3f,
    @location(5) shadow0: vec3f,
    @location(6) shadow1: vec3f,
    @location(7) shadow2: vec3f,
    @location(8) shadow3: vec3f,
};

@vertex
fn vs_main(in: VsIn) -> VsOut {
    var out: VsOut;
    let position = u_terrain.model * vec4f(in.position, 1.0);
    let eye = u_terrain.view * position;
    out.clip = fix_clip(u_terrain.projection * eye);
    out.color = in.color;
    out.uv0 = in.uv0;
    out.uv1 = in.uv1;
    out.normal = normalize((u_terrain.normal_matrix * vec4f(in.normal, 0.0)).xyz);
    out.position = position.xyz;
    let sh = project_shadows(position.xyz);
    out.shadow0 = sh[0];
    out.shadow1 = sh[1];
    out.shadow2 = sh[2];
    out.shadow3 = sh[3];
    return out;
}

@fragment
fn fs_main(in: VsOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    var albedo = in.color * u_terrain.albedo_color;
    albedo = albedo * textureSample(t_albedo, s_albedo, in.uv0);

    let detail = textureSample(t_detail, s_detail, in.uv1).rgb;
    albedo = vec4f(albedo.rgb * detail, albedo.a);

    var projected: array<vec3f, 4>;
    projected[0] = in.shadow0;
    projected[1] = in.shadow1;
    projected[2] = in.shadow2;
    projected[3] = in.shadow3;
    var shadow = calculate_shadow(projected);
    shadow = mix(0.5, 1.0, shadow);

    let normal = select(-in.normal, in.normal, front);

    let emissive = u_terrain.emissive_color * textureSample(t_emissive, s_emissive, in.uv0).rgb;

    let params = textureSample(t_material, s_material, in.uv0).xyz;
    let ambient_occlusion = 1.0 + u_terrain.ao_strength * (params.r - 1.0);
    let roughness = u_terrain.roughness * params.g;
    let metalness = u_terrain.metalness * params.b;

    var color = calculate_lighting(in.position, normal, albedo.rgb, emissive, shadow,
                                   ambient_occlusion, roughness, metalness);

    let dist = length(u_light.camera_position - in.position);
    let fog_amount = clamp((dist - u_terrain.fog_range.x) / (u_terrain.fog_range.y - u_terrain.fog_range.x), 0.0, 1.0);
    color = mix(color, u_terrain.fog_color, fog_amount);

    return vec4f(color, 1.0);
}
