#version 450

layout(location = 0) in vec3 vertex_color;
layout(location = 1) in vec3 world_normal;
layout(location = 0) out vec4 output_color;

layout(set = 0, binding = 0) uniform CameraUniforms
{
    mat4 view;
    mat4 projection;
    vec3 light_direction;
    float light_direction_padding;
    vec3 light_color;
    float light_intensity;
} camera;

void main()
{
    const vec3 normal = normalize(world_normal);
    const vec3 direction_to_light = normalize(-camera.light_direction);
    const float diffuse_factor = max(dot(normal, direction_to_light), 0.0);
    const vec3 ambient = vec3(0.1);
    const vec3 diffuse = diffuse_factor * camera.light_color * camera.light_intensity;
    output_color = vec4(vertex_color * (ambient + diffuse), 1.0);
}
