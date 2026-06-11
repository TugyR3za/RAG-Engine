#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec3 vertex_color;
layout(location = 1) out vec3 world_normal;

layout(set = 0, binding = 0) uniform CameraUniforms
{
    mat4 view;
    mat4 projection;
    vec3 light_direction;
    float light_direction_padding;
    vec3 light_color;
    float light_intensity;
} camera;

layout(push_constant) uniform ObjectPushConstants
{
    mat4 model;
} object;

void main()
{
    gl_Position = camera.projection * camera.view * object.model * vec4(in_position, 1.0);
    vertex_color = in_color;
    world_normal = normalize(mat3(object.model) * in_normal);
}
