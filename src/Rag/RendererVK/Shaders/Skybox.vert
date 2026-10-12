#version 450

// Prefix of the existing per-frame camera UBO, including the backend's Y flip.
layout(set = 0, binding = 0) uniform CameraUniforms
{
    mat4 view;
    mat4 projection;
} camera;

layout(location = 0) out vec3 sky_direction;

void main()
{
    const vec2 positions[3] = vec2[3](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
    vec2 clip_xy = positions[gl_VertexIndex];
    // RH_ZO depth, cleared to 1. No finite cube or near-plane clipping involved.
    gl_Position = vec4(clip_xy, 1.0, 1.0);
    vec4 near_position = inverse(camera.projection) * vec4(clip_xy, 0.0, 1.0);
    // Orthographic rays are parallel. A distant environment is consequently one
    // direction across that viewport. Perspective rays vary with FOV and aspect.
    vec3 view_direction = camera.projection[3][3] == 0.0
        ? near_position.xyz / near_position.w : vec3(0, 0, -1);
    // Inverse of the rigid view rotation; deliberately omit camera translation.
    sky_direction = transpose(mat3(camera.view)) * view_direction;
}
