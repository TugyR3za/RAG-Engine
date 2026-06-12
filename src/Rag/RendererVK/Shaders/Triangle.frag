#version 450

layout(location = 0) in vec3 vertex_color;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec3 world_position;
layout(location = 3) in vec4 light_space_position;
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
} camera;

// Directional shadow map sampled through a depth-comparison sampler.
layout(set = 0, binding = 1) uniform sampler2DShadow shadow_map;

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

    // 3x3 PCF. Each comparison tap returns 0..1 (and is itself 2x2-filtered when
    // the device supports linear depth filtering), so this softens shadow edges.
    vec2 texel_size = 1.0 / vec2(textureSize(shadow_map, 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texel_size;
            lit += texture(shadow_map, vec3(shadow_uv + offset, reference_depth));
        }
    }
    return lit / 9.0;
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

    // Shadowed fragments keep ambient but lose diffuse + specular, so shadows
    // are dark but not pure black.
    const float lit = ComputeLitFactor(normal, direction_to_light);
    output_color = vec4((vertex_color * (ambient + (lit * diffuse))) + (lit * specular), 1.0);
}
