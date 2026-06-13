#pragma once

#include "Rag/RendererVK/Internal/VulkanVertexBuffer.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace rag::renderer::vk
{
    struct GltfMeshData
    {
        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::vector<u8> base_color_pixels;
        u32 texture_width = 0;
        u32 texture_height = 0;
        std::string mesh_name;
        bool texture_embedded = false;
    };

    // Loads the first primitive of the first mesh in a binary glTF file from
    // the runtime models/ folder. POSITION, NORMAL, TEXCOORD_0, indices, and
    // the material's base-color texture are required. The transform chain of
    // the first node referencing that mesh is baked into positions and normals.
    [[nodiscard]] GltfMeshData LoadGltfMesh(
        std::string_view gltf_filename,
        std::array<f32, 3> base_color);
}
