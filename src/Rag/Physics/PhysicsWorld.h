#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Core/Math.h"
#include "Rag/Physics/PhysicsTypes.h"

#include <memory>

namespace rag::physics
{
    struct PhysicsWorldDesc
    {
        // Downward gravity for the whole world.
        math::Vec3 gravity{0.0f, -9.81f, 0.0f};

        // Reference physics step. Each frame Jolt runs an integer number of
        // collision steps chosen so every internal step is roughly this long,
        // which keeps integration stable regardless of frame rate.
        f32 fixed_timestep_seconds = 1.0f / 60.0f;

        // Frame deltas larger than this are clamped before stepping so a hitch,
        // a breakpoint, or a paused/resumed app cannot explode the simulation
        // with one enormous timestep.
        f32 max_frame_delta_seconds = 0.033f;
    };

    // A minimal rigid-body world built on Jolt Physics. Every Jolt type is kept
    // behind a PIMPL, so this header (and anything that includes it) never sees a
    // Jolt header and the module depends downward on RagCore only.
    class PhysicsWorld
    {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        // Boots the Jolt globals and a PhysicsSystem with two object layers
        // (NON_MOVING / MOVING). Returns false if initialization failed.
        [[nodiscard]] bool Initialize(const PhysicsWorldDesc& desc);

        // Releases every body and all Jolt resources. Safe to call repeatedly.
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const;

        // Static (immovable) box collider, e.g. the ground plane.
        BodyHandle CreateStaticBox(const math::Vec3& position, const math::Vec3& half_extents);

        // Dynamic rigid bodies that fall under gravity and collide.
        BodyHandle CreateDynamicBox(const math::Vec3& position, const math::Vec3& half_extents);
        BodyHandle CreateDynamicSphere(const math::Vec3& position, f32 radius);

        // Advances the simulation. delta_seconds is clamped to the configured
        // maximum before being handed to Jolt.
        void Step(f32 delta_seconds);

        // Reads a body's current world transform back out of the simulation.
        [[nodiscard]] BodyState GetBodyState(BodyHandle handle) const;

        // Updates world gravity live (used by the editor's gravity slider).
        void SetGravity(const math::Vec3& gravity);

        // Teleports a dynamic body back to a pose, clears its velocity, and
        // re-activates it (used by the editor's "reset bodies" button).
        void ResetDynamicBody(BodyHandle handle, const math::Vec3& position, const math::Quat& rotation);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
