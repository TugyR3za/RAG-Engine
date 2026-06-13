#include "Rag/RendererVK/Internal/VulkanMeshLoader.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanAssetPath.h"

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif
#define TINYOBJLOADER_IMPLEMENTATION
// The embedded fast_float parser fails to compile on MSVC /std:c++20 (C3615);
// the classic parser is plenty for a one-time startup load.
#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#include <tiny_obj_loader.h>
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

#include <bit>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace rag::renderer::vk
{
    namespace
    {
        // Exact-match dedup key: the bit patterns of position + normal. Color
        // is uniform across the mesh, so it never distinguishes vertices.
        struct VertexKey
        {
            std::array<u32, 6> bits{};

            bool operator==(const VertexKey&) const = default;
        };

        struct VertexKeyHash
        {
            [[nodiscard]] std::size_t operator()(const VertexKey& key) const
            {
                // FNV-1a over the six 32-bit words.
                std::size_t hash = 14695981039346656037ull;
                for (const u32 word : key.bits)
                {
                    hash ^= word;
                    hash *= 1099511628211ull;
                }
                return hash;
            }
        };

        [[nodiscard]] VertexKey MakeVertexKey(
            const std::array<f32, 3>& position,
            const std::array<f32, 3>& normal)
        {
            return VertexKey{{
                std::bit_cast<u32>(position[0]),
                std::bit_cast<u32>(position[1]),
                std::bit_cast<u32>(position[2]),
                std::bit_cast<u32>(normal[0]),
                std::bit_cast<u32>(normal[1]),
                std::bit_cast<u32>(normal[2])}};
        }

        [[nodiscard]] std::array<f32, 3> FaceNormal(
            const std::array<f32, 3>& a,
            const std::array<f32, 3>& b,
            const std::array<f32, 3>& c)
        {
            const f32 ab_x = b[0] - a[0];
            const f32 ab_y = b[1] - a[1];
            const f32 ab_z = b[2] - a[2];
            const f32 ac_x = c[0] - a[0];
            const f32 ac_y = c[1] - a[1];
            const f32 ac_z = c[2] - a[2];

            const f32 normal_x = (ab_y * ac_z) - (ab_z * ac_y);
            const f32 normal_y = (ab_z * ac_x) - (ab_x * ac_z);
            const f32 normal_z = (ab_x * ac_y) - (ab_y * ac_x);
            const f32 length =
                std::sqrt((normal_x * normal_x) + (normal_y * normal_y) + (normal_z * normal_z));

            if (length <= 0.000001f)
            {
                // Degenerate (zero-area) face; any unit vector shades harmlessly.
                return {0.0f, 1.0f, 0.0f};
            }

            return {normal_x / length, normal_y / length, normal_z / length};
        }
    }

    ObjMeshData LoadObjMesh(std::string_view obj_filename, std::array<f32, 3> base_color)
    {
        const std::filesystem::path path = ResolveRuntimeAssetPath(
            "models",
            obj_filename,
            "Rebuild RagSandbox so CMake can copy the model assets next to the executable.");

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warning;
        std::string error;

        // Default arguments triangulate, so faces always arrive as triangles.
        const bool loaded =
            tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, path.string().c_str());

        if (!warning.empty())
        {
            RAG_LOG_WARNING("OBJ loader warning for '", obj_filename, "': ", warning);
        }

        if (!loaded)
        {
            const std::string message =
                "Failed to parse OBJ model '" +
                path.string() +
                "': " +
                (error.empty() ? std::string("unknown tinyobjloader error.") : error);
            RAG_LOG_ERROR(message);
            throw VulkanError(message);
        }

        const std::size_t position_count = attrib.vertices.size() / 3;
        const std::size_t normal_count = attrib.normals.size() / 3;

        std::size_t corner_count = 0;
        for (const tinyobj::shape_t& shape : shapes)
        {
            corner_count += shape.mesh.indices.size();
        }

        if (corner_count == 0 || (corner_count % 3) != 0)
        {
            throw VulkanError(
                "OBJ model '" + path.string() + "' contains no triangulated faces.");
        }

        ObjMeshData data;
        data.vertices.reserve(corner_count);
        data.indices.reserve(corner_count);

        std::unordered_map<VertexKey, u32, VertexKeyHash> unique_vertices;
        unique_vertices.reserve(corner_count);

        std::size_t corners_with_file_normals = 0;

        for (const tinyobj::shape_t& shape : shapes)
        {
            const std::vector<tinyobj::index_t>& shape_indices = shape.mesh.indices;
            for (std::size_t corner = 0; corner + 2 < shape_indices.size(); corner += 3)
            {
                std::array<std::array<f32, 3>, 3> positions{};
                for (std::size_t i = 0; i < 3; ++i)
                {
                    const int vertex_index = shape_indices[corner + i].vertex_index;
                    if (vertex_index < 0 || static_cast<std::size_t>(vertex_index) >= position_count)
                    {
                        throw VulkanError(
                            "OBJ model '" + path.string() + "' references a position outside its vertex list.");
                    }

                    positions[i] = {
                        attrib.vertices[(3 * static_cast<std::size_t>(vertex_index)) + 0],
                        attrib.vertices[(3 * static_cast<std::size_t>(vertex_index)) + 1],
                        attrib.vertices[(3 * static_cast<std::size_t>(vertex_index)) + 2]};
                }

                const std::array<f32, 3> face_normal = FaceNormal(positions[0], positions[1], positions[2]);

                for (std::size_t i = 0; i < 3; ++i)
                {
                    const int normal_index = shape_indices[corner + i].normal_index;
                    std::array<f32, 3> normal = face_normal;
                    if (normal_index >= 0 && static_cast<std::size_t>(normal_index) < normal_count)
                    {
                        normal = {
                            attrib.normals[(3 * static_cast<std::size_t>(normal_index)) + 0],
                            attrib.normals[(3 * static_cast<std::size_t>(normal_index)) + 1],
                            attrib.normals[(3 * static_cast<std::size_t>(normal_index)) + 2]};
                        ++corners_with_file_normals;
                    }

                    const VertexKey key = MakeVertexKey(positions[i], normal);
                    const auto [entry, inserted] =
                        unique_vertices.try_emplace(key, static_cast<u32>(data.vertices.size()));
                    if (inserted)
                    {
                        data.vertices.push_back(Vertex{positions[i], normal, base_color});
                    }
                    data.indices.push_back(entry->second);
                }
            }
        }

        data.normals_from_file = corners_with_file_normals == corner_count;

        std::string normals_description;
        if (data.normals_from_file)
        {
            normals_description = "loaded from file";
        }
        else if (corners_with_file_normals == 0)
        {
            normals_description = "generated flat per-face";
        }
        else
        {
            normals_description =
                "mixed (" +
                std::to_string(corners_with_file_normals) +
                " of " +
                std::to_string(corner_count) +
                " corners from file, rest generated flat per-face)";
        }

        RAG_LOG_INFO(
            "Loaded OBJ model '",
            obj_filename,
            "': vertices=",
            data.vertices.size(),
            ", indices=",
            data.indices.size(),
            ", normals=",
            normals_description,
            ", deduplicated from ",
            corner_count,
            " face corners.");

        return data;
    }
}
