#version 450

// Flat-colored fragment for the selection highlight box. The color is shared with
// the vertex stage through the same push-constant block so the pipeline needs a
// single push-constant range covering both stages.
layout(push_constant) uniform OutlinePushConstants
{
    vec3 center;
    vec3 extents;
    vec3 color;
} box;

layout(location = 0) out vec4 output_color;

void main()
{
    output_color = vec4(box.color, 1.0);
}
