#include "Rag/RendererVK/Internal/VulkanGltfLoader.h"

#include "Rag/Core/Log.h"
#include "Rag/Core/Math.h"
#include "Rag/RendererVK/Internal/VulkanAssetPath.h"

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace rag::renderer::vk
{
    namespace
    {
        struct AccessorView
        {
            const u8* data = nullptr;
            std::size_t stride = 0;
            std::size_t count = 0;
        };

        [[nodiscard]] AccessorView GetAccessorView(
            const tinygltf::Model& model,
            int accessor_index,
            int expected_type,
            int expected_component_type,
            std::string_view semantic,
            const std::filesystem::path& path)
        {
            if (accessor_index < 0 ||
                static_cast<std::size_t>(accessor_index) >= model.accessors.size())
            {
                throw VulkanError(
                    "glTF model '" + path.string() + "' has an invalid " +
                    std::string(semantic) + " accessor.");
            }

            const tinygltf::Accessor& accessor =
                model.accessors[static_cast<std::size_t>(accessor_index)];
            if (accessor.type != expected_type ||
                accessor.componentType != expected_component_type ||
                accessor.sparse.isSparse)
            {
                throw VulkanError(
                    "glTF model '" + path.string() + "' has an unsupported " +
                    std::string(semantic) + " accessor layout.");
            }

            if (accessor.bufferView < 0 ||
                static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size())
            {
                throw VulkanError(
                    "glTF model '" + path.string() + "' has no buffer view for " +
                    std::string(semantic) + ".");
            }

            const tinygltf::BufferView& view =
                model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
            if (view.buffer < 0 ||
                static_cast<std::size_t>(view.buffer) >= model.buffers.size())
            {
                throw VulkanError(
                    "glTF model '" + path.string() + "' has an invalid buffer for " +
                    std::string(semantic) + ".");
            }

            const int stride = accessor.ByteStride(view);
            if (stride <= 0)
            {
                throw VulkanError(
                    "glTF model '" + path.string() + "' has an invalid stride for " +
                    std::string(semantic) + ".");
            }

            const tinygltf::Buffer& buffer =
                model.buffers[static_cast<std::size_t>(view.buffer)];
            const std::size_t offset = view.byteOffset + accessor.byteOffset;
            const std::size_t element_size =
                static_cast<std::size_t>(tinygltf::GetComponentSizeInBytes(
                    static_cast<u32>(accessor.componentType))) *
                static_cast<std::size_t>(tinygltf::GetNumComponentsInType(
                    static_cast<u32>(accessor.type)));

            if (accessor.count == 0 ||
                offset > buffer.data.size() ||
                element_size > buffer.data.size() - offset ||
                (accessor.count > 1 &&
                 static_cast<std::size_t>(stride) >
                     (buffer.data.size() - offset - element_size) / (accessor.count - 1)))
            {
                throw VulkanError(
                    "glTF model '" + path.string() + "' has out-of-range " +
                    std::string(semantic) + " data.");
            }

            return AccessorView{
                buffer.data.data() + offset,
                static_cast<std::size_t>(stride),
                accessor.count};
        }

        template <std::size_t ComponentCount>
        [[nodiscard]] std::array<f32, ComponentCount> ReadFloatVector(
            const AccessorView& view,
            std::size_t index)
        {
            std::array<f32, ComponentCount> result{};
            std::memcpy(
                result.data(),
                view.data + (view.stride * index),
                sizeof(f32) * ComponentCount);
            return result;
        }

        [[nodiscard]] u32 ReadIndex(
            const AccessorView& view,
            std::size_t index,
            int component_type)
        {
            const u8* source = view.data + (view.stride * index);
            switch (component_type)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return *source;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                u16 value = 0;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                u32 value = 0;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            default:
                throw VulkanError("glTF index accessor uses an unsupported component type.");
            }
        }

        [[nodiscard]] math::Mat4 QuaternionRotation(const std::vector<double>& quaternion)
        {
            if (quaternion.size() != 4)
            {
                return math::Identity();
            }

            const f32 x = static_cast<f32>(quaternion[0]);
            const f32 y = static_cast<f32>(quaternion[1]);
            const f32 z = static_cast<f32>(quaternion[2]);
            const f32 w = static_cast<f32>(quaternion[3]);

            math::Mat4 result = math::Identity();
            result(0, 0) = 1.0f - (2.0f * y * y) - (2.0f * z * z);
            result(0, 1) = (2.0f * x * y) - (2.0f * z * w);
            result(0, 2) = (2.0f * x * z) + (2.0f * y * w);
            result(1, 0) = (2.0f * x * y) + (2.0f * z * w);
            result(1, 1) = 1.0f - (2.0f * x * x) - (2.0f * z * z);
            result(1, 2) = (2.0f * y * z) - (2.0f * x * w);
            result(2, 0) = (2.0f * x * z) - (2.0f * y * w);
            result(2, 1) = (2.0f * y * z) + (2.0f * x * w);
            result(2, 2) = 1.0f - (2.0f * x * x) - (2.0f * y * y);
            return result;
        }

        [[nodiscard]] math::Mat4 NodeLocalTransform(const tinygltf::Node& node)
        {
            if (node.matrix.size() == 16)
            {
                math::Mat4 result{};
                for (std::size_t index = 0; index < node.matrix.size(); ++index)
                {
                    result.elements[index] = static_cast<f32>(node.matrix[index]);
                }
                return result;
            }

            math::Vec3 translation{};
            if (node.translation.size() == 3)
            {
                translation = math::Vec3{
                    static_cast<f32>(node.translation[0]),
                    static_cast<f32>(node.translation[1]),
                    static_cast<f32>(node.translation[2])};
            }

            math::Vec3 scale{1.0f, 1.0f, 1.0f};
            if (node.scale.size() == 3)
            {
                scale = math::Vec3{
                    static_cast<f32>(node.scale[0]),
                    static_cast<f32>(node.scale[1]),
                    static_cast<f32>(node.scale[2])};
            }

            return math::Multiply(
                math::Multiply(math::Translation(translation), QuaternionRotation(node.rotation)),
                math::Scale(scale));
        }

        [[nodiscard]] math::Mat4 MeshNodeTransform(
            const tinygltf::Model& model,
            int mesh_index)
        {
            int mesh_node = -1;
            std::vector<int> parents(model.nodes.size(), -1);
            for (std::size_t parent_index = 0; parent_index < model.nodes.size(); ++parent_index)
            {
                const tinygltf::Node& parent = model.nodes[parent_index];
                for (const int child : parent.children)
                {
                    if (child >= 0 && static_cast<std::size_t>(child) < parents.size())
                    {
                        parents[static_cast<std::size_t>(child)] =
                            static_cast<int>(parent_index);
                    }
                }

                if (mesh_node < 0 && parent.mesh == mesh_index)
                {
                    mesh_node = static_cast<int>(parent_index);
                }
            }

            if (mesh_node < 0)
            {
                return math::Identity();
            }

            std::vector<int> chain;
            for (int node = mesh_node; node >= 0; node = parents[static_cast<std::size_t>(node)])
            {
                chain.push_back(node);
                if (chain.size() > model.nodes.size())
                {
                    throw VulkanError("glTF node hierarchy contains a cycle.");
                }
            }

            math::Mat4 transform = math::Identity();
            for (auto node = chain.rbegin(); node != chain.rend(); ++node)
            {
                transform = math::Multiply(
                    transform,
                    NodeLocalTransform(model.nodes[static_cast<std::size_t>(*node)]));
            }
            return transform;
        }

        [[nodiscard]] std::array<f32, 3> TransformPosition(
            const math::Mat4& transform,
            const std::array<f32, 3>& value)
        {
            return {
                (transform(0, 0) * value[0]) + (transform(0, 1) * value[1]) +
                    (transform(0, 2) * value[2]) + transform(0, 3),
                (transform(1, 0) * value[0]) + (transform(1, 1) * value[1]) +
                    (transform(1, 2) * value[2]) + transform(1, 3),
                (transform(2, 0) * value[0]) + (transform(2, 1) * value[1]) +
                    (transform(2, 2) * value[2]) + transform(2, 3)};
        }

        [[nodiscard]] std::array<f32, 3> TransformNormal(
            const math::Mat4& transform,
            const std::array<f32, 3>& value)
        {
            const f32 a00 = transform(0, 0);
            const f32 a01 = transform(0, 1);
            const f32 a02 = transform(0, 2);
            const f32 a10 = transform(1, 0);
            const f32 a11 = transform(1, 1);
            const f32 a12 = transform(1, 2);
            const f32 a20 = transform(2, 0);
            const f32 a21 = transform(2, 1);
            const f32 a22 = transform(2, 2);
            const f32 determinant =
                (a00 * ((a11 * a22) - (a12 * a21))) -
                (a01 * ((a10 * a22) - (a12 * a20))) +
                (a02 * ((a10 * a21) - (a11 * a20)));

            if (std::abs(determinant) <= 0.000001f)
            {
                const math::Vec3 normal = math::TransformDirection(
                    transform,
                    math::Vec3{value[0], value[1], value[2]});
                return {normal.x, normal.y, normal.z};
            }

            const f32 inverse_determinant = 1.0f / determinant;
            const math::Vec3 normal = math::Normalize(math::Vec3{
                (((a11 * a22) - (a12 * a21)) * value[0] +
                 ((a12 * a20) - (a10 * a22)) * value[1] +
                 ((a10 * a21) - (a11 * a20)) * value[2]) *
                    inverse_determinant,
                (((a02 * a21) - (a01 * a22)) * value[0] +
                 ((a00 * a22) - (a02 * a20)) * value[1] +
                 ((a01 * a20) - (a00 * a21)) * value[2]) *
                    inverse_determinant,
                (((a01 * a12) - (a02 * a11)) * value[0] +
                 ((a02 * a10) - (a00 * a12)) * value[1] +
                 ((a00 * a11) - (a01 * a10)) * value[2]) *
                    inverse_determinant});
            return {normal.x, normal.y, normal.z};
        }
    }

    GltfMeshData LoadGltfMesh(
        std::string_view gltf_filename,
        std::array<f32, 3> base_color)
    {
        const std::filesystem::path path = ResolveRuntimeAssetPath(
            "models",
            gltf_filename,
            "Rebuild RagSandbox so CMake can copy the model assets next to the executable.");

        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string warning;
        std::string error;
        const bool loaded = loader.LoadBinaryFromFile(
            &model,
            &error,
            &warning,
            path.string());

        if (!warning.empty())
        {
            RAG_LOG_WARNING("glTF loader warning for '", gltf_filename, "': ", warning);
        }
        if (!loaded)
        {
            const std::string message =
                "Failed to parse glTF model '" + path.string() + "': " +
                (error.empty() ? std::string("unknown tinygltf error.") : error);
            RAG_LOG_ERROR(message);
            throw VulkanError(message);
        }
        if (model.meshes.empty() || model.meshes.front().primitives.empty())
        {
            throw VulkanError("glTF model '" + path.string() + "' contains no mesh primitive.");
        }

        const tinygltf::Mesh& mesh = model.meshes.front();
        const tinygltf::Primitive& primitive = mesh.primitives.front();
        if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES)
        {
            throw VulkanError("glTF model '" + path.string() + "' first primitive is not triangles.");
        }

        const auto find_attribute = [&](std::string_view name) -> int {
            const auto attribute = primitive.attributes.find(std::string(name));
            if (attribute == primitive.attributes.end())
            {
                throw VulkanError(
                    "glTF model '" + path.string() + "' first primitive is missing " +
                    std::string(name) + ".");
            }
            return attribute->second;
        };

        const AccessorView positions = GetAccessorView(
            model,
            find_attribute("POSITION"),
            TINYGLTF_TYPE_VEC3,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "POSITION",
            path);
        const AccessorView normals = GetAccessorView(
            model,
            find_attribute("NORMAL"),
            TINYGLTF_TYPE_VEC3,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "NORMAL",
            path);
        const AccessorView texcoords = GetAccessorView(
            model,
            find_attribute("TEXCOORD_0"),
            TINYGLTF_TYPE_VEC2,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "TEXCOORD_0",
            path);

        if (positions.count != normals.count || positions.count != texcoords.count)
        {
            throw VulkanError(
                "glTF model '" + path.string() + "' vertex attribute counts do not match.");
        }
        if (positions.count > std::numeric_limits<u32>::max())
        {
            throw VulkanError("glTF model has too many vertices for RAG Engine mesh indices.");
        }

        if (primitive.indices < 0 ||
            static_cast<std::size_t>(primitive.indices) >= model.accessors.size())
        {
            throw VulkanError("glTF model '" + path.string() + "' has no valid index accessor.");
        }
        const tinygltf::Accessor& index_accessor =
            model.accessors[static_cast<std::size_t>(primitive.indices)];
        if (index_accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
            index_accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
            index_accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
        {
            throw VulkanError("glTF model uses an unsupported index component type.");
        }
        const AccessorView index_view = GetAccessorView(
            model,
            primitive.indices,
            TINYGLTF_TYPE_SCALAR,
            index_accessor.componentType,
            "indices",
            path);

        const math::Mat4 node_transform = MeshNodeTransform(model, 0);
        GltfMeshData data;
        data.mesh_name = mesh.name;
        data.vertices.reserve(positions.count);
        for (std::size_t vertex_index = 0; vertex_index < positions.count; ++vertex_index)
        {
            data.vertices.push_back(Vertex{
                TransformPosition(
                    node_transform,
                    ReadFloatVector<3>(positions, vertex_index)),
                TransformNormal(
                    node_transform,
                    ReadFloatVector<3>(normals, vertex_index)),
                base_color,
                ReadFloatVector<2>(texcoords, vertex_index)});
        }

        data.indices.reserve(index_view.count);
        for (std::size_t index = 0; index < index_view.count; ++index)
        {
            const u32 vertex_index =
                ReadIndex(index_view, index, index_accessor.componentType);
            if (vertex_index >= data.vertices.size())
            {
                throw VulkanError("glTF model index references a vertex outside the vertex array.");
            }
            data.indices.push_back(vertex_index);
        }

        if (primitive.material < 0 ||
            static_cast<std::size_t>(primitive.material) >= model.materials.size())
        {
            throw VulkanError("glTF model first primitive has no valid material.");
        }
        const tinygltf::Material& material =
            model.materials[static_cast<std::size_t>(primitive.material)];
        const int texture_index = material.pbrMetallicRoughness.baseColorTexture.index;
        if (texture_index < 0 ||
            static_cast<std::size_t>(texture_index) >= model.textures.size())
        {
            throw VulkanError("glTF model material has no valid baseColorTexture.");
        }
        const tinygltf::Texture& texture =
            model.textures[static_cast<std::size_t>(texture_index)];
        if (texture.source < 0 ||
            static_cast<std::size_t>(texture.source) >= model.images.size())
        {
            throw VulkanError("glTF model base color texture has no valid image source.");
        }
        const tinygltf::Image& image =
            model.images[static_cast<std::size_t>(texture.source)];
        if (image.width <= 0 ||
            image.height <= 0 ||
            image.component != 4 ||
            image.bits != 8 ||
            image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
            image.image.empty())
        {
            throw VulkanError(
                "glTF base color image must decode to non-empty RGBA8 pixels.");
        }

        const u64 expected_image_bytes =
            static_cast<u64>(image.width) * static_cast<u64>(image.height) * 4u;
        if (expected_image_bytes != image.image.size())
        {
            throw VulkanError("glTF base color image byte count does not match its dimensions.");
        }

        data.texture_width = static_cast<u32>(image.width);
        data.texture_height = static_cast<u32>(image.height);
        data.base_color_pixels.assign(image.image.begin(), image.image.end());
        data.texture_embedded = image.bufferView >= 0 && image.uri.empty();

        RAG_LOG_INFO(
            "Loaded glTF model '",
            gltf_filename,
            "': mesh=\"",
            data.mesh_name.empty() ? std::string("<unnamed>") : data.mesh_name,
            "\", vertices=",
            data.vertices.size(),
            ", indices=",
            data.indices.size(),
            ", base_color_texture=",
            data.texture_embedded ? "embedded" : "external",
            ", texture=",
            data.texture_width,
            "x",
            data.texture_height,
            " RGBA8.");

        return data;
    }
}
