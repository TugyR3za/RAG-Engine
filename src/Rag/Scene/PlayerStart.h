#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Scene/Components.h"

#include <string>

namespace rag::scene
{
    // Supported ranges for PlayerStartComponent. SanitizePlayerStart enforces
    // them wherever a configuration enters the engine: scene load, Inspector
    // edits, and the spawn on Play, so the character controller never sees a
    // degenerate capsule or a NaN speed.
    struct PlayerStartLimits
    {
        static constexpr f32 MinMoveSpeed = 0.1f;
        static constexpr f32 MaxMoveSpeed = 50.0f;
        static constexpr f32 MinMouseSensitivityDegrees = 0.005f;
        static constexpr f32 MaxMouseSensitivityDegrees = 2.0f;
        static constexpr f32 MinJumpHeight = 0.0f;
        static constexpr f32 MaxJumpHeight = 10.0f;
        static constexpr f32 MinCapsuleRadius = 0.1f;
        static constexpr f32 MaxCapsuleRadius = 1.0f;
        // The straight (cylinder) part of the capsule must not vanish: height is
        // at least 2 * radius + MinCylinderHeight.
        static constexpr f32 MinCylinderHeight = 0.1f;
        static constexpr f32 MaxCapsuleHeight = 4.0f;
        static constexpr f32 MinEyeHeight = 0.1f;
        static constexpr f32 MaxAbsFallResetHeight = 100000.0f;
    };

    // The smallest valid total capsule height for a given radius.
    [[nodiscard]] constexpr f32 MinPlayerCapsuleHeight(f32 radius)
    {
        return (2.0f * radius) + PlayerStartLimits::MinCylinderHeight;
    }

    // Corrects a configuration in place. A non-finite field takes its default;
    // every other field is clamped into the ranges above, in dependency order
    // (radius, then height >= MinPlayerCapsuleHeight(radius), then eye height
    // within the capsule). Yaw is wrapped into [-pi, pi]. Returns one
    // "field old -> new" entry per correction (empty when nothing changed) so
    // callers can report it.
    [[nodiscard]] std::string SanitizePlayerStart(PlayerStartComponent& config);
}
