// Object renderer WGSL (WP9). Port of gl33/object_vs.glsl + object_fs.glsl.
// Composed as: preamble + shadow + lighting + object.
// group(0) shared scene (lighting, shadows), group(1) ObjectUniforms, group(2) textures.

// C++ mirror: CWebGPUObjectRenderer::Uniforms (wgpu_object_renderer.h)
struct ObjectUniforms {
    projection: mat4x4f,
    view: mat4x4f,
    model: mat4x4f,
    normal_matrix: mat4x4f,     // upper 3x3 used
    albedo_color: vec4f,
    emissive_color: vec3f,
    roughness: f32,
    recolor_from: vec3f,        // HSV
    metalness: f32,
    recolor_to: vec3f,          // HSV
    ao_strength: f32,
    fog_color: vec3f,
    recolor_threshold: f32,
    fog_range: vec2f,
    uv_offset: vec2f,
    uv_scale: vec2f,
    alpha_scissor: f32,
    triplanar_scale: f32,
    triplanar_mode: u32,
    recolor: u32,
    _pad0: u32,
    _pad1: u32,
};  // 384 bytes

@group(1) @binding(0) var<uniform> u_object: ObjectUniforms;

@group(2) @binding(0) var t_albedo: texture_2d<f32>;
@group(2) @binding(1) var s_albedo: sampler;
@group(2) @binding(2) var t_detail: texture_2d<f32>;
@group(2) @binding(3) var s_detail: sampler;
@group(2) @binding(4) var t_emissive: texture_2d<f32>;
@group(2) @binding(5) var s_emissive: sampler;
@group(2) @binding(6) var t_material: texture_2d<f32>;
@group(2) @binding(7) var s_material: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv0: vec2f,
    @location(4) uv1: vec2f,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4f,
    @location(0) color: vec4f,
    @location(1) uv0: vec2f,
    @location(2) uv1: vec2f,
    @location(3) normal: vec3f,
    @location(4) vertex_coord: vec3f,
    @location(5) vertex_normal: vec3f,
    @location(6) world_position: vec3f,
    @location(7) shadow0: vec3f,
    @location(8) shadow1: vec3f,
    @location(9) shadow2: vec3f,
    @location(10) shadow3: vec3f,
};

@vertex
fn vs_main(vin: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let position = u_object.model * vec4f(vin.position, 1.0);
    let eye_space = u_object.view * position;
    out.clip_position = fix_clip(u_object.projection * eye_space);

    let nm = mat3x3f(u_object.normal_matrix[0].xyz, u_object.normal_matrix[1].xyz, u_object.normal_matrix[2].xyz);

    out.color = vin.color;
    out.uv0 = vin.uv0 * u_object.uv_scale + u_object.uv_offset;
    out.uv1 = vin.uv1;
    out.normal = normalize(nm * vin.normal);
    out.vertex_coord = vin.position;
    out.vertex_normal = vin.normal;
    out.world_position = position.xyz;

    let shadows = project_shadows(position.xyz);
    out.shadow0 = shadows[0];
    out.shadow1 = shadows[1];
    out.shadow2 = shadows[2];
    out.shadow3 = shadows[3];
    return out;
}

// All components are in the range [0...1], including hue.
fn rgb2hsv(c: vec3f) -> vec3f {
    let K = vec4f(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    let p = mix(vec4f(c.bg, K.wz), vec4f(c.gb, K.xy), step(c.b, c.g));
    let q = mix(vec4f(p.xyw, c.r), vec4f(c.r, p.yzx), step(p.x, c.r));
    let d = q.x - min(q.w, q.y);
    let e = 1.0e-10;
    return vec3f(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// All components are in the range [0...1], including hue.
fn hsv2rgb(c: vec3f) -> vec3f {
    let K = vec4f(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    let p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, vec3f(0.0), vec3f(1.0)), c.y);
}

fn triplanar(position: vec3f, normal: vec3f) -> vec3f {
    let weights = normal * normal;
    var sum = textureSample(t_detail, s_detail, position.yz).rgb * weights.x;
    sum += textureSample(t_detail, s_detail, position.zx).rgb * weights.y;
    sum += textureSample(t_detail, s_detail, position.xy).rgb * weights.z;
    return sum;
}

@fragment
fn fs_main(vin: VertexOutput, @builtin(front_facing) front_facing: bool) -> @location(0) vec4f {
    // All implicit-derivative samples are taken up front (uniform control flow).
    var tex_color = textureSample(t_albedo, s_albedo, vin.uv0);
    let emissive_tex = textureSample(t_emissive, s_emissive, vin.uv0).rgb;
    let params = textureSample(t_material, s_material, vin.uv0).xyz;

    var dirty: vec3f;
    if (u_object.triplanar_mode != 0u) {
        dirty = triplanar(vin.vertex_coord * u_object.triplanar_scale, vin.vertex_normal);
    } else {
        dirty = textureSample(t_detail, s_detail, vin.uv1).rgb;
    }

    var albedo = vin.color * u_object.albedo_color;

    if (u_object.recolor != 0u) {
        var hsv = rgb2hsv(tex_color.rgb);
        if (abs(hsv.x - u_object.recolor_from.x) < u_object.recolor_threshold) {
            hsv.x += u_object.recolor_to.x - u_object.recolor_from.x;
            hsv.y += u_object.recolor_to.y - u_object.recolor_from.y;
            if (hsv.x < 0.0) { hsv.x += 1.0; }
            if (hsv.x > 1.0) { hsv.x -= 1.0; }
            hsv.y = clamp(hsv.y, 0.0, 1.0);
        }
        tex_color = vec4f(hsv2rgb(hsv), tex_color.a);
    }

    albedo *= tex_color;
    albedo = vec4f(albedo.rgb * dirty, albedo.a);

    var color = albedo.rgb;
    let alpha = albedo.a;

    if (u_light.lighting != 0u) {
        let projected = array<vec3f, 4>(vin.shadow0, vin.shadow1, vin.shadow2, vin.shadow3);
        let shadow = mix(0.5, 1.0, calculate_shadow(projected));

        let normal = select(-vin.normal, vin.normal, front_facing);
        let emissive = u_object.emissive_color * emissive_tex;

        let ambient_occlusion = 1.0 + u_object.ao_strength * (params.r - 1.0);
        let roughness = u_object.roughness * params.g;
        let metalness = u_object.metalness * params.b;

        color = calculate_lighting(vin.world_position, normal, color, emissive, shadow,
                                   ambient_occlusion, roughness, metalness);
    }

    let dist = length(u_light.camera_position - vin.world_position);
    let fog_amount = clamp((dist - u_object.fog_range.x) / (u_object.fog_range.y - u_object.fog_range.x), 0.0, 1.0);
    color = mix(color, u_object.fog_color, fog_amount);

    if (alpha < u_object.alpha_scissor) { discard; }

    return vec4f(color, alpha);
}
