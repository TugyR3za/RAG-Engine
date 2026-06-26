#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Core/Math.h"

namespace rag::physics
{
    // Opaque handle to a body living inside a PhysicsWorld. It wraps a Jolt
    // BodyID value but deliberately exposes none of Jolt's headers so the rest
    // of the engine can depend on physics without depending on Jolt.
    struct BodyHandle
    {
        u32 id = 0xFFFFFFFFu;

        [[nodiscard]] bool IsValid() const
        {
            return id != 0xFFFFFFFFu;
        }
    };

    // World-space transform of a body, expressed in the engine's math types.
    struct BodyState
    {
        math::Vec3 position{};
        math::Quat rotation{};
    };
}
