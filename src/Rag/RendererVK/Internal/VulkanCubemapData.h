#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Renderer/Environment.h"

#include <vector>

namespace rag::renderer::vk
{
    struct CubemapData
    {
        u32 size = 0;
        // Six contiguous RGBA8 faces in the documented axis order.
        std::vector<u8> pixels;
    };

    // CPU-only validation/decoding; throws with the offending path on failure.
    [[nodiscard]] CubemapData LoadCubemapData(const SkyboxFaces& faces, u32 max_dimension);
}
