#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_texcoord;

layout(location = 0) out vec3 vertex_color;
layout(location = 1) out vec3 world_normal;
layout(location = 2) out vec3 world_position;
layout(location = 3) out vec4 light_space_position;
layout(location = 4) out vec2 texture_uv;

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

layout(push_constant) uniform ObjectPushConstants
{
    mat4 model;
} object;

void main()
{
    const vec4 position_world = object.model * vec4(in_position, 1.0);
    gl_Position = camera.projection * camera.view * position_world;
    vertex_color = in_color;
    world_normal = normalize(mat3(object.model) * in_normal);
    world_position = position_world.xyz;
    // Position in the light's clip space for the shadow lookup. The light uses an
    // orthographic projection (w == 1), so this interpolates linearly.
    light_space_position = camera.light_space * position_world;
    texture_uv = in_texcoord;
}
