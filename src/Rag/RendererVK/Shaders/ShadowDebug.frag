#version 450

// Samples the shadow map's raw depth (binding 2 is a plain, non-comparison
// sampler) and shows it as grayscale: near the light is dark, far is bright.
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 output_color;

layout(set = 0, binding = 2) uniform sampler2D shadow_depth;

void main()
{
    float depth = texture(shadow_depth, in_uv).r;
    output_color = vec4(vec3(depth), 1.0);
}
