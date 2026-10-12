#pragma once

#include <array>
#include <string>

namespace rag::renderer
{
    // Project-relative image paths, in +X, -X, +Y, -Y, +Z, -Z order.
    // Images are top-row-first; the exact face axes are documented in docs/skybox.md.
    using SkyboxFaces = std::array<std::string, 6>;

    struct EnvironmentSettings
    {
        // Legacy scenes and new empty scenes retain the clear-color background.
        bool skybox_enabled = false;
        SkyboxFaces skybox_faces{};

        bool operator==(const EnvironmentSettings&) const = default;
    };

    // Checks authoring data only, without filesystem or GPU access. An unconfigured
    // disabled environment is valid; otherwise all six paths must be provided.
    [[nodiscard]] bool ValidateEnvironment(const EnvironmentSettings& environment, std::string& error);
}
