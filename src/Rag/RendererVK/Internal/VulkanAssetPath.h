#pragma once

#include <filesystem>
#include <string_view>

namespace rag::renderer::vk
{
    // Resolves a runtime asset that the build copies next to RagSandbox.exe
    // (shaders, models). Searches <exe dir>/<subdirectory>/<filename>, then
    // <cwd>/<subdirectory>/<filename>, then <cwd>/<filename>. Throws
    // VulkanError with `missing_hint` appended when no candidate exists.
    [[nodiscard]] std::filesystem::path ResolveRuntimeAssetPath(
        std::string_view subdirectory,
        std::string_view filename,
        std::string_view missing_hint);
}
