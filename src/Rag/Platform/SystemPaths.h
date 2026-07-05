#pragma once

#include <filesystem>

namespace rag::platform
{
    // Absolute directory that contains the running executable. Assets the build
    // copies next to the binary (models/, assets/, shaders/) resolve relative to
    // this, independent of the process's current working directory, so tools can
    // find them however the app was launched. Falls back to the current working
    // directory if the platform query fails. Implemented per platform (Win32
    // uses GetModuleFileNameW) so no native headers leak into callers.
    [[nodiscard]] std::filesystem::path ExecutableDirectory();
}
