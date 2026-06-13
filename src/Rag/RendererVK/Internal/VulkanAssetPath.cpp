#include "Rag/RendererVK/Internal/VulkanAssetPath.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanCommon.h"

#include <array>
#include <string>
#include <system_error>
#include <vector>

namespace rag::renderer::vk
{
    namespace
    {
        std::filesystem::path ExecutableDirectory()
        {
#if defined(RAG_PLATFORM_WINDOWS)
            std::vector<wchar_t> buffer(512);

            for (;;)
            {
                const DWORD length = GetModuleFileNameW(
                    nullptr,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()));

                if (length == 0)
                {
                    throw VulkanError("Failed to resolve the RAG Sandbox executable path.");
                }

                if (length < buffer.size())
                {
                    return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
                }

                buffer.resize(buffer.size() * 2);
            }
#else
            return std::filesystem::current_path();
#endif
        }
    }

    std::filesystem::path ResolveRuntimeAssetPath(
        std::string_view subdirectory,
        std::string_view filename,
        std::string_view missing_hint)
    {
        const std::filesystem::path executable_directory = ExecutableDirectory();
        const std::filesystem::path current_directory = std::filesystem::current_path();
        const std::array candidates = {
            executable_directory / subdirectory / filename,
            current_directory / subdirectory / filename,
            current_directory / filename,
        };

        std::error_code error;
        for (const std::filesystem::path& candidate : candidates)
        {
            if (std::filesystem::is_regular_file(candidate, error))
            {
                return candidate;
            }
            error.clear();
        }

        const std::string message =
            "Failed to locate runtime asset '" +
            std::string(filename) +
            "'. Expected it at '" +
            candidates.front().string() +
            "'. " +
            std::string(missing_hint);
        RAG_LOG_ERROR(message);
        throw VulkanError(message);
    }
}
