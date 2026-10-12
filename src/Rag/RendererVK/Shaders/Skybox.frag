#version 450

layout(set = 1, binding = 0) uniform samplerCube skybox;
layout(location = 0) in vec3 sky_direction;
layout(location = 0) out vec4 out_color;

void main()
{
    // sRGB texture sampling supplies linear RGB, just like existing materials.
    out_color = vec4(texture(skybox, sky_direction).rgb, 1.0);
}
