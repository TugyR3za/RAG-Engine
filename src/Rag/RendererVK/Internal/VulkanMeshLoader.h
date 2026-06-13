#pragma once

#include "Rag/RendererVK/Internal/VulkanVertexBuffer.h"

#include <array>
#include <string_view>
#include <vector>

namespace rag::renderer::vk
{
    struct ObjMeshData
    {
        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        // True when every face corner carried a normal index in the .obj file;
        // otherwise missing normals were generated flat per face.
        bool normals_from_file = false;
    };

    // Loads a .obj from the runtime models/ folder (copied next to the
    // executable at build time) into the engine's Vertex format. Faces are
    // triangulated by the loader, every vertex receives `base_color` (.obj
    // carries no color), and repeated position/normal combinations are
    // deduplicated so the index buffer references shared vertices.
    // Throws VulkanError when the file is missing or fails to parse.
    [[nodiscard]] ObjMeshData LoadObjMesh(std::string_view obj_filename, std::array<f32, 3> base_color);
}
