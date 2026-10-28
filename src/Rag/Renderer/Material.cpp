#include "Rag/Renderer/Material.h"

#include <algorithm>
#include <cmath>

namespace rag::renderer
{
    f32 SrgbToLinear(f32 encoded)
    {
        if (!std::isfinite(encoded))
        {
            return 1.0f;
        }

        const f32 value = std::clamp(encoded, 0.0f, 1.0f);
        return value <= 0.04045f
            ? value / 12.92f
            : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    math::Vec3 SrgbToLinear(math::Vec3 encoded)
    {
        return math::Vec3{
            SrgbToLinear(encoded.x),
            SrgbToLinear(encoded.y),
            SrgbToLinear(encoded.z)};
    }
}
