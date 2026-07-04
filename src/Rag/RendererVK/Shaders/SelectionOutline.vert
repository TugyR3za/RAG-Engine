#version 450

// Editor selection highlight: draws the 12 edges of a world-space, axis-aligned
// bounding box as a line list (24 vertices, no vertex buffer). Each vertex is a
// box corner placed at center +/- extents; which corner comes from gl_VertexIndex.
// The camera view/projection are shared with the main pass through set 0,
// binding 0, so the box lines up exactly with the shaded scene.

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

layout(push_constant) uniform OutlinePushConstants
{
    vec3 center;
    vec3 extents;
    vec3 color;
} box;

// The 8 box corners as sign vectors (+/-1 per axis), expanded into the 24
// endpoints of the 12 edges: the bottom ring, the top ring, then the 4 verticals.
const vec3 kCornerSigns[24] = vec3[](
    vec3(-1.0, -1.0, -1.0), vec3( 1.0, -1.0, -1.0),
    vec3( 1.0, -1.0, -1.0), vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0), vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0, -1.0), vec3(-1.0, -1.0, -1.0),

    vec3(-1.0, -1.0,  1.0), vec3( 1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0), vec3( 1.0,  1.0,  1.0),
    vec3( 1.0,  1.0,  1.0), vec3(-1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0), vec3(-1.0, -1.0,  1.0),

    vec3(-1.0, -1.0, -1.0), vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0, -1.0), vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0, -1.0), vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0, -1.0), vec3(-1.0,  1.0,  1.0));

void main()
{
    const vec3 world_position = box.center + (kCornerSigns[gl_VertexIndex] * box.extents);
    gl_Position = camera.projection * camera.view * vec4(world_position, 1.0);
}
