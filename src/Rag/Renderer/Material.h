#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Core/Math.h"

namespace rag::renderer
{
    // Per-object base-color authoring settings. Each RenderableComponent stores its
    // own copy by value, so editing one entity never affects another entity that
    // shares the same mesh or texture; no GPU resource is owned or referenced here.
    //
    // Color convention: base_color is sRGB-encoded RGB in [0, 1] -- the same space
    // as base-color texture pixels and as the values shown by the Inspector's color
    // picker (so 0.5 means hex 0x80, not 50% linear light). Scene render extraction
    // converts it to linear exactly once (SrgbToLinear); the backend multiplies that
    // with the hardware-decoded sRGB texture and the sRGB swapchain encodes the
    // result. No other stage applies a gamma curve.
    //
    // Shading:
    //   use_base_color_texture == true:  albedo = base-color texture * base_color
    //   use_base_color_texture == false: albedo = base_color
    //   no texture resolved for the mesh: the texture term is opaque white, so the
    //                                     result equals base_color alone.
    // Lighting and shadows are applied on top of the albedo in every case.
    //
    // The defaults (white, texture on) reproduce the pre-material appearance.
    struct MaterialSettings
    {
        math::Vec3 base_color{1.0f, 1.0f, 1.0f};
        bool use_base_color_texture = true;

        [[nodiscard]] bool operator==(const MaterialSettings& other) const
        {
            return base_color.x == other.base_color.x &&
                   base_color.y == other.base_color.y &&
                   base_color.z == other.base_color.z &&
                   use_base_color_texture == other.use_base_color_texture;
        }
    };

    // IEC 61966-2-1 sRGB electro-optical transfer function (piecewise, not a 2.2
    // power approximation), matching what VK_FORMAT_*_SRGB sampling does in
    // hardware. Inputs are clamped to [0, 1]; non-finite inputs map to 1 (white).
    [[nodiscard]] f32 SrgbToLinear(f32 encoded);
    [[nodiscard]] math::Vec3 SrgbToLinear(math::Vec3 encoded);
}
