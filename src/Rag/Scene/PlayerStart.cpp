#include "Rag/Scene/PlayerStart.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace rag::scene
{
    namespace
    {
        // Applies one field's rule and records the change, if any.
        void FixField(
            const char* name,
            f32& value,
            f32 fallback,
            f32 minimum,
            f32 maximum,
            std::ostringstream& report)
        {
            const f32 original = value;
            const f32 corrected = std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
            if (corrected == original)
            {
                return;
            }
            value = corrected;
            if (report.tellp() > 0)
            {
                report << "; ";
            }
            report << name << ' ' << original << " -> " << corrected;
        }
    }

    std::string SanitizePlayerStart(PlayerStartComponent& config)
    {
        using Limits = PlayerStartLimits;
        const PlayerStartComponent defaults{};
        std::ostringstream report;

        // Yaw only needs wrapping; any finite angle is a valid facing.
        if (!std::isfinite(config.yaw_radians) ||
            config.yaw_radians < -math::Pi || config.yaw_radians > math::Pi)
        {
            const f32 original = config.yaw_radians;
            config.yaw_radians = std::isfinite(original)
                ? static_cast<f32>(std::remainder(original, 2.0f * math::Pi))
                : defaults.yaw_radians;
            report << "yawRadians " << original << " -> " << config.yaw_radians;
        }

        FixField("moveSpeed", config.move_speed, defaults.move_speed,
            Limits::MinMoveSpeed, Limits::MaxMoveSpeed, report);
        FixField("mouseSensitivityDegrees", config.mouse_sensitivity_degrees, defaults.mouse_sensitivity_degrees,
            Limits::MinMouseSensitivityDegrees, Limits::MaxMouseSensitivityDegrees, report);
        FixField("jumpHeight", config.jump_height, defaults.jump_height,
            Limits::MinJumpHeight, Limits::MaxJumpHeight, report);
        FixField("capsuleRadius", config.capsule_radius, defaults.capsule_radius,
            Limits::MinCapsuleRadius, Limits::MaxCapsuleRadius, report);
        // The default height is valid for any radius up to 0.85 m; for wider
        // capsules a non-finite height falls back to the minimum instead.
        FixField("capsuleHeight", config.capsule_height,
            std::max(defaults.capsule_height, MinPlayerCapsuleHeight(config.capsule_radius)),
            MinPlayerCapsuleHeight(config.capsule_radius), Limits::MaxCapsuleHeight, report);
        FixField("eyeHeight", config.eye_height, std::min(defaults.eye_height, config.capsule_height),
            Limits::MinEyeHeight, config.capsule_height, report);
        FixField("fallResetHeight", config.fall_reset_height, defaults.fall_reset_height,
            -Limits::MaxAbsFallResetHeight, Limits::MaxAbsFallResetHeight, report);
        return report.str();
    }
}
