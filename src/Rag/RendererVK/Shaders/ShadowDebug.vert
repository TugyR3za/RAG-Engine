#version 450

// RAG_SHADOW_DEBUG overlay: a small quad pinned to the top-left corner that
// shows the raw shadow map. No vertex buffer; positions/uvs come from the index.
// NDC (-1, -1) is the top-left of the screen in Vulkan, so uv (0, 0) maps there
// and the depth map is shown right-side-up.
layout(location = 0) out vec2 out_uv;

vec2 positions[6] = vec2[](
    vec2(-0.98, -0.98),
    vec2(-0.98, -0.48),
    vec2(-0.48, -0.48),
    vec2(-0.98, -0.98),
    vec2(-0.48, -0.48),
    vec2(-0.48, -0.98));

vec2 uvs[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0));

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    out_uv = uvs[gl_VertexIndex];
}
