#version 450

layout(location = 0) out vec3 vertex_color;

const vec2 positions[3] = vec2[](
    vec2(0.0, -0.65),
    vec2(0.6, 0.5),
    vec2(-0.6, 0.5)
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.15, 0.1),
    vec3(0.1, 1.0, 0.2),
    vec3(0.1, 0.35, 1.0)
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vertex_color = colors[gl_VertexIndex];
}
