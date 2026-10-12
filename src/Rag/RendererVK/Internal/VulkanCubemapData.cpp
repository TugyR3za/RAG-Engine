#include "Rag/RendererVK/Internal/VulkanCubemapData.h"

#include "Rag/RendererVK/Internal/VulkanAssetPath.h"
#include "Rag/RendererVK/Internal/VulkanCommon.h"

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif
#include <stb_image.h>
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

#include <algorithm>
#include <limits>
#include <memory>

namespace rag::renderer::vk
{
    namespace
    {
        [[nodiscard]] std::string StbiFailureReason()
        {
            const char* reason = stbi_failure_reason();
            return reason != nullptr ? reason : "unknown image decoding error";
        }
    }

    CubemapData LoadCubemapData(const SkyboxFaces& faces, u32 max_dimension)
    {
        std::string error;
        if (!ValidateEnvironment(EnvironmentSettings{true, faces}, error)) throw VulkanError(error);

        CubemapData result;
        std::array<std::filesystem::path, 6> paths;
        // Validate every header before allocating or uploading any image data.
        for (std::size_t i = 0; i < faces.size(); ++i)
        {
            paths[i] = ResolveRuntimeAssetPath("", faces[i], "Check the scene's skyboxFaces paths.");
            int width = 0, height = 0, channels = 0;
            if (!stbi_info(paths[i].string().c_str(), &width, &height, &channels))
                throw VulkanError("Cannot read skybox face '" + paths[i].string() + "': " + StbiFailureReason());
            if (width <= 0 || width != height || static_cast<u32>(width) > max_dimension ||
                (i != 0 && static_cast<u32>(width) != result.size))
                throw VulkanError("Invalid skybox face '" + paths[i].string() + "' (" +
                    std::to_string(width) + "x" + std::to_string(height) +
                    "): all faces must be square, match dimensions, and fit the device cube-image limit " +
                    std::to_string(max_dimension) + ".");
            result.size = static_cast<u32>(width);
        }
        const u64 face_bytes = static_cast<u64>(result.size) * result.size * 4;
        if (face_bytes > std::numeric_limits<std::size_t>::max() / 6)
            throw VulkanError("Skybox pixel size overflow: '" + faces[0] + "'.");
        result.pixels.resize(static_cast<std::size_t>(face_bytes * 6));
        for (std::size_t i = 0; i < faces.size(); ++i)
        {
            int width = 0, height = 0, channels = 0;
            // Like existing texture loads: original top row first, no flip/rotation.
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
                stbi_load(paths[i].string().c_str(), &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
            if (!pixels)
                throw VulkanError("Cannot decode skybox face '" + paths[i].string() + "': " + StbiFailureReason());
            if (width != static_cast<int>(result.size) || height != width)
                throw VulkanError("Skybox face changed dimensions during load: '" + paths[i].string() + "'.");
            std::copy_n(pixels.get(), static_cast<std::size_t>(face_bytes),
                result.pixels.data() + static_cast<std::size_t>(i * face_bytes));
        }
        return result;
    }
}
