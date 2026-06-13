#version 450

layout(location = 0) in vec3 vertex_color;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec3 world_position;
layout(location = 3) in vec4 light_space_position;
layout(location = 4) in vec2 texture_uv;
layout(location = 0) out vec4 output_color;

layout(set = 0, binding = 0) uniform CameraUniforms
{
    mat4 view;
    mat4 projection;
    mat4 light_space;
    vec3 light_direction;
    float light_direction_padding;
    vec3 light_color;
    float light_intensity;
    vec3 camera_position;
    float camera_position_padding;
    int shadow_pcf_kernel_radius;
    float shadow_ambient_floor;
    float shadow_tuning_padding_0;
    float shadow_tuning_padding_1;
} camera;

// Directional shadow map sampled through a depth-comparison sampler.
layout(set = 0, binding = 1) uniform sampler2DShadow shadow_map;
layout(set = 1, binding = 0) uniform sampler2D color_texture;

// Depth bias (in light clip-space depth units) used to fight shadow acne.
// Increase if you see acne (self-shadowing stripes); decrease if shadows
// visibly detach from their casters (peter-panning).
const float kShadowMinBias = 0.0012;
const float kShadowMaxBias = 0.0035;

// Returns the lit fraction in [0, 1]: 1.0 fully lit, 0.0 fully shadowed.
float ComputeLitFactor(vec3 normal, vec3 direction_to_light)
{
    // Perspective divide is a no-op for the orthographic light, but keep it
    // general. xy go from clip [-1, 1] to texture [0, 1]; z is already the
    // Vulkan [0, 1] depth, directly comparable to the stored depth.
    vec3 projected = light_space_position.xyz / light_space_position.w;
    vec2 shadow_uv = projected.xy * 0.5 + 0.5;
    float current_depth = projected.z;

    // Fragments past the light's far plane or outside its frustum are lit.
    if (current_depth > 1.0 ||
        shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||
        shadow_uv.y < 0.0 || shadow_uv.y > 1.0)
    {
        return 1.0;
    }

    float n_dot_l = max(dot(normal, direction_to_light), 0.0);
    float bias = max(kShadowMaxBias * (1.0 - n_dot_l), kShadowMinBias);
    float reference_depth = current_depth - bias;

    // Configurable PCF. Radius 1 is 3x3; radius 2 is 5x5. Each comparison tap
    // returns 0..1 and may itself be linearly filtered by the depth sampler.
    int kernel_radius = clamp(camera.shadow_pcf_kernel_radius, 0, 3);
    vec2 texel_size = 1.0 / vec2(textureSize(shadow_map, 0));
    float lit = 0.0;
    for (int x = -kernel_radius; x <= kernel_radius; ++x)
    {
        for (int y = -kernel_radius; y <= kernel_radius; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texel_size;
            lit += texture(shadow_map, vec3(shadow_uv + offset, reference_depth));
        }
    }
    int kernel_width = (kernel_radius * 2) + 1;
    return lit / float(kernel_width * kernel_width);
}

void main()
{
    const float shininess = 32.0;
    const float specular_strength = 0.4;
    const vec3 normal = normalize(world_normal);
    const vec3 direction_to_light = normalize(-camera.light_direction);
    const vec3 direction_to_camera = normalize(camera.camera_position - world_position);
    const vec3 halfway_direction = normalize(direction_to_light + direction_to_camera);
    const float diffuse_factor = max(dot(normal, direction_to_light), 0.0);
    const float specular_factor =
        pow(max(dot(normal, halfway_direction), 0.0), shininess) * specular_strength;
    const vec3 ambient = vec3(0.1);
    const vec3 diffuse = diffuse_factor * camera.light_color * camera.light_intensity;
    const vec3 specular = specular_factor * camera.light_color * camera.light_intensity;
    const vec3 albedo = texture(color_texture, texture_uv).rgb;

    // Keep a configurable fraction of direct light in fully shadowed areas so
    // shadows read as natural dark gray instead of near-black.
    const float lit = ComputeLitFactor(normal, direction_to_light);
    const float shadow_visibility =
        mix(clamp(camera.shadow_ambient_floor, 0.0, 1.0), 1.0, lit);
    output_color = vec4(
        (albedo * (ambient + (shadow_visibility * diffuse))) +
            (shadow_visibility * specular),
        1.0);
}
