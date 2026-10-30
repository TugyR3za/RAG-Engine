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

        // Number of bodies Jolt currently holds (static and dynamic). Lets callers
        // verify that destroying/rebuilding a scene leaves no leaked or ghost body.
        [[nodiscard]] u32 BodyCount() const;

        // Creates a body from a full descriptor (collider primitive + offset +
        // pose + scale + motion type + gravity + CCD). This is the general entry
        // point used by the editor to (re)build a body when its mode changes.
        // Returns an invalid handle for BodyDesc::mode == None.
        BodyHandle CreateBody(const BodyDesc& desc);

        // Removes a body from the simulation and frees it. Safe on an invalid
        // handle. Used when an entity switches to BodyMode::None (or is rebuilt).
        void DestroyBody(BodyHandle handle);

        // Convenience wrappers over CreateBody for the common primitives. Static
        // boxes never move; dynamic boxes/spheres fall under gravity with CCD on.
        BodyHandle CreateStaticBox(const math::Vec3& position, const math::Vec3& half_extents);
        BodyHandle CreateDynamicBox(const math::Vec3& position, const math::Vec3& half_extents);
        BodyHandle CreateDynamicSphere(const math::Vec3& position, f32 radius);

        // Advances the simulation. delta_seconds is clamped to the configured
        // maximum before being handed to Jolt.
        void Step(f32 delta_seconds);

        // Reads a body's current world transform back out of the simulation.
        [[nodiscard]] BodyState GetBodyState(BodyHandle handle) const;

        // Replaces a body's collider with a shape derived from its immutable
        // creation-time base shape. Box scale is per-axis. Sphere scale uses the
        // largest axis uniformly so the collider conservatively encloses a
        // non-uniformly scaled visual sphere. For a dynamic body it also clears
        // velocity so a resize does not fling the body; for a static body it
        // simply swaps the shape. Returns false for an invalid handle or if Jolt
        // cannot create the scaled shape.
        [[nodiscard]] bool SetBodyScale(BodyHandle handle, const math::Vec3& scale);

        // Enables/disables gravity on a dynamic body live (gravity factor 1/0). A
        // dynamic body with gravity disabled stays put but still collides and can
        // be pushed. No-op for a static/invalid body.
        void SetBodyGravityEnabled(BodyHandle handle, bool enabled);

        // Updates world gravity live (used by the editor's gravity slider).
        void SetGravity(const math::Vec3& gravity);

        // Teleports a body to a pose. For a dynamic body it also clears velocity
        // and re-activates it (used by transform edits and the "reset bodies"
        // button); for a static body it just repositions the collider.
        void SetBodyPose(BodyHandle handle, const math::Vec3& position, const math::Quat& rotation);

        // --- Character controllers (first-person player) ----------------------
        //
        // A character is moved explicitly with MoveCharacter, never by Step: the
        // caller decides its velocity (gameplay rules live outside physics) and
        // interleaves MoveCharacter with Step. Destroying it, or Shutdown, frees
        // it; it never adds to BodyCount.

        // Creates an upright capsule character at desc.feet_position. Returns an
        // invalid handle if the world is not initialized or the capsule is
        // degenerate (height must exceed 2 * radius). It does not check for
        // overlap; use IsCharacterSpaceFree first to avoid spawning inside geometry.
        CharacterHandle CreateCharacter(const CharacterDesc& desc);
        void DestroyCharacter(CharacterHandle handle);
        [[nodiscard]] u32 CharacterCount() const;

        // Sweeps the capsule along velocity * delta_seconds, sliding along
        // whatever it hits; it does not move through static or dynamic colliders.
        // Dynamic bodies in the way receive an impulse limited by the character's
        // max_push_force. When the character walks off a step (and is not rising)
        // it is kept on the floor up to 0.5 m below. There is no stair climbing.
        // gravity_y only weights what the character stands on; the velocity
        // passed in already includes any gravity the caller integrated.
        CharacterState MoveCharacter(
            CharacterHandle handle,
            const math::Vec3& velocity,
            f32 delta_seconds,
            f32 gravity_y);

        [[nodiscard]] CharacterState GetCharacterState(CharacterHandle handle) const;

        // Teleports the character's feet to feet_position and refreshes its
        // contacts (so the ground state is valid immediately). No sweep is done.
        void SetCharacterPosition(CharacterHandle handle, const math::Vec3& feet_position);

        // True when an upright capsule (radius, total height) with its feet at
        // feet_position would not penetrate any static or dynamic collider by more
        // than a centimetre. Touching (e.g. resting on the floor) counts as free.
        [[nodiscard]] bool IsCharacterSpaceFree(
            f32 radius,
            f32 height,
            const math::Vec3& feet_position) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
