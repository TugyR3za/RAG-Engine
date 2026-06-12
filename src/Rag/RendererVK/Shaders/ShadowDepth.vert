#version 450

// Depth-only shadow pass: project scene geometry into the directional light's
// clip space. Only the position attribute is used; normal/color are ignored.
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

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

layout(push_constant) uniform ObjectPushConstants
{
    mat4 model;
} object;

void main()
{
    gl_Position = camera.light_space * object.model * vec4(in_position, 1.0);
}
