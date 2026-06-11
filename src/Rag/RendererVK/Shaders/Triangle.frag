#version 450

layout(location = 0) in vec3 vertex_color;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec3 world_position;
layout(location = 0) out vec4 output_color;

layout(set = 0, binding = 0) uniform CameraUniforms
{
    mat4 view;
    mat4 projection;
    vec3 light_direction;
    float light_direction_padding;
    vec3 light_color;
    float light_intensity;
    vec3 camera_position;
    float camera_position_padding;
} camera;

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
    output_color = vec4((vertex_color * (ambient + diffuse)) + specular, 1.0);
}
