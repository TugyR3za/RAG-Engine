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

    // How an entity participates in the simulation. Exposed per-object in the
    // editor and used to create/destroy/reconfigure the underlying Jolt body.
    //   None    - not simulated at all (pure visual; no collider).
    //   Static  - collides but never moves under simulation (ground, buildings).
    //   Dynamic - full rigid body: falls, is pushed, collides.
    enum class BodyMode : u8
    {
        None,
        Static,
        Dynamic
    };

    // The primitive a body's collider is built from. Half-extents / radius in a
    // BodyDesc are the UNSCALED base that matches the mesh at scale 1; the
    // entity transform's scale is applied on top (see collider-follows-scale).
    enum class ColliderShape : u8
    {
        Box,
        Sphere
    };

    // A resolution-independent description of a body: its collider primitive plus
    // simulation settings. Kept in the scene/editor layer as the source of truth
    // so a body can be rebuilt with the correct collider when its mode changes
    // (e.g. None -> Dynamic) without losing shape/scale information.
    struct BodyDesc
    {
        ColliderShape shape = ColliderShape::Box;

        // Base collider extents at scale 1. box_half_extents is used for boxes,
        // sphere_radius for spheres. Choose these to match the mesh's unit size.
        math::Vec3 box_half_extents{0.5f, 0.5f, 0.5f};
        f32 sphere_radius = 0.5f;

        // Local offset of the collider from the entity origin. Non-zero for meshes
        // whose pivot is not at their centre (e.g. the duck rests on its pivot).
        math::Vec3 offset{0.0f, 0.0f, 0.0f};

        // Spawn pose + initial scale. Scale is baked into the collider at creation.
        math::Vec3 position{0.0f, 0.0f, 0.0f};
        math::Quat rotation{};
        math::Vec3 scale{1.0f, 1.0f, 1.0f};

        // Simulation settings. mode must be Static or Dynamic to produce a body;
        // None is handled by the caller (no body is created). gravity_enabled and
        // continuous_collision only apply to Dynamic bodies.
        BodyMode mode = BodyMode::Dynamic;
        bool gravity_enabled = true;
        bool continuous_collision = true; // CCD via Jolt LinearCast motion quality
    };
}
