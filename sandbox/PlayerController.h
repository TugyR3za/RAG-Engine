#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Core/Math.h"

#include <algorithm>
#include <cmath>

namespace rag::runtime
{
    // First-person movement rules, kept free of physics, scene and input types so
    // they can be unit-tested with a fake mover. Collision is not done here: each
    // substep the controller decides a velocity and hands it to a mover callback
    // (in the sandbox, PhysicsWorld::MoveCharacter, a Jolt CharacterVirtual
    // sweep), then reconciles its velocity with where the capsule actually ended.
    //
    // Conventions: metres, seconds, radians; +Y up; yaw 0 looks down -Z and
    // positive yaw turns left, exactly like the editor's free-fly camera, so the
    // view can be built from Euler (pitch, yaw, 0) the same way.

    // What supports the player after a substep (the mover's report, simplified).
    // Only OnGround allows jumping and walking at full control; steep ground
    // behaves like air (gravity applies and the capsule slides).
    enum class PlayerGround : u8
    {
        OnGround,
        OnSteepGround,
        Unsupported
    };

    // The tunable part of a Player Start while the player runs. The sandbox copies
    // it from the (already sanitized) authored PlayerStartComponent.
    struct PlayerMovementSettings
    {
        f32 move_speed = 4.5f;               // m/s on the ground
        f32 mouse_sensitivity_degrees = 0.1f; // per raw mouse count
        f32 jump_height = 1.0f;              // apex height in metres; 0 = no jumping
        f32 eye_height = 1.6f;               // camera above the feet
        f32 fall_reset_height = -25.0f;      // world Y
    };

    // One frame of intent. The sandbox fills it from live input only while the
    // mouse is captured and the simulation is Playing; otherwise it stays zero,
    // so Paused frames, single Steps and typing in the editor never move the
    // player or turn the view.
    struct PlayerIntent
    {
        f32 forward = 0.0f;        // +1 W, -1 S
        f32 right = 0.0f;          // +1 D, -1 A
        bool jump_pressed = false; // a NEW press this frame (an edge, never "held")
        f32 look_x = 0.0f;         // raw mouse counts this frame, +X = right
        f32 look_y = 0.0f;         // +Y = down (screen convention)
    };

    struct PlayerMoveResult
    {
        math::Vec3 feet_position{};
        PlayerGround ground = PlayerGround::Unsupported;
    };

    // Same limit as the editor camera: never quite straight up or down.
    inline constexpr f32 PlayerMaxPitchRadians = (math::Pi * 0.5f) - 0.01f;

    // Steering available while airborne (m/s^2). With no input the player keeps
    // its horizontal momentum, so walking off a ledge carries it forward.
    inline constexpr f32 PlayerAirAcceleration = 8.0f;

    // How many equal substeps an advance is split into: the same ceil(advance /
    // tick) rule PhysicsWorld::Step uses for its collision steps, so a substep
    // is never longer than one tick and a Paused Step (exactly one tick) is one.
    [[nodiscard]] inline u32 SimulationSubsteps(f32 advance_seconds, f32 tick_seconds)
    {
        if (!(advance_seconds > 0.0f) || !(tick_seconds > 0.0f))
        {
            return 0;
        }
        return std::max(1u, static_cast<u32>(std::ceil(advance_seconds / tick_seconds)));
    }

    [[nodiscard]] inline math::Vec3 PlayerHorizontalForward(f32 yaw_radians)
    {
        return math::Vec3{-std::sin(yaw_radians), 0.0f, -std::cos(yaw_radians)};
    }

    [[nodiscard]] inline math::Vec3 PlayerHorizontalRight(f32 yaw_radians)
    {
        return math::Vec3{std::cos(yaw_radians), 0.0f, -std::sin(yaw_radians)};
    }

    // Movement direction on the horizontal plane for the given axes. Longer than
    // one only for diagonals, which are normalized so W+D is not faster than W.
    [[nodiscard]] inline math::Vec3 PlayerWishDirection(f32 yaw_radians, f32 forward, f32 right)
    {
        const math::Vec3 wish =
            (PlayerHorizontalForward(yaw_radians) * std::clamp(forward, -1.0f, 1.0f)) +
            (PlayerHorizontalRight(yaw_radians) * std::clamp(right, -1.0f, 1.0f));
        const f32 length = math::Length(wish);
        return length > 1.0f ? wish * (1.0f / length) : wish;
    }

    // Take-off speed that reaches jump_height under gravity_y. Gravity weaker
    // than 0.1 m/s^2 (the Debug panel slider allows 0 or even upward gravity) is
    // treated as 0.1 so the speed stays finite.
    [[nodiscard]] inline f32 PlayerJumpSpeed(f32 jump_height, f32 gravity_y)
    {
        const f32 gravity = std::max(-gravity_y, 0.1f);
        return std::sqrt(2.0f * gravity * std::max(jump_height, 0.0f));
    }

    class PlayerController
    {
    public:
        // Spawn and fall reset: put the feet at feet_position facing yaw_radians,
        // level the view, clear all motion and any pending jump, and take the
        // mover's ground report for the new spot.
        void Reset(const math::Vec3& feet_position, f32 yaw_radians, PlayerGround ground)
        {
            feet_ = feet_position;
            yaw_ = WrapYaw(yaw_radians);
            pitch_ = 0.0f;
            velocity_ = math::Vec3{};
            ground_ = ground;
            ClearIntent();
        }

        // The authored start that fall resets return to (see NeedsFallReset).
        void SetStartPosition(const math::Vec3& start_feet_position)
        {
            start_feet_ = start_feet_position;
        }

        void SetSettings(const PlayerMovementSettings& settings)
        {
            settings_ = settings;
        }

        [[nodiscard]] const PlayerMovementSettings& Settings() const { return settings_; }

        // Mouse look for one frame: raw counts -> degrees -> radians. Moving the
        // mouse right turns right, down looks down; pitch is clamped.
        void ApplyLook(f32 delta_x, f32 delta_y)
        {
            const f32 radians_per_count = math::ToRadians(settings_.mouse_sensitivity_degrees);
            yaw_ = WrapYaw(yaw_ - (delta_x * radians_per_count));
            pitch_ = std::clamp(pitch_ - (delta_y * radians_per_count), -PlayerMaxPitchRadians, PlayerMaxPitchRadians);
        }

        // Latches this frame's movement axes for every substep of the frame and
        // arms at most one jump, which only the first substep can use.
        void BeginFrame(const PlayerIntent& intent)
        {
            forward_ = intent.forward;
            right_ = intent.right;
            jump_pending_ = intent.jump_pressed;
        }

        // Drops held movement and any pending jump (capture released, Pause).
        void ClearIntent()
        {
            forward_ = 0.0f;
            right_ = 0.0f;
            jump_pending_ = false;
        }

        // Advances one substep of dt seconds under gravity_y. mover(velocity, dt)
        // must sweep the capsule and return a PlayerMoveResult.
        //
        //   * On walkable ground (and not already rising) vertical motion is
        //     cancelled; a pending jump launches at PlayerJumpSpeed. One press
        //     gives at most one jump: the request is spent on this substep
        //     whether or not the player could jump.
        //   * On the ground horizontal velocity is exactly the wish velocity; in
        //     the air it steers toward it at PlayerAirAcceleration, or keeps its
        //     momentum with no input.
        //   * Gravity is integrated at the step midpoint, so a jump reaches the
        //     same apex at any frame rate (no collision in the way).
        //   * Afterwards velocity is cut back to what the mover achieved: a head
        //     bump ends the rise, landing ends the fall, a wall ends airborne
        //     momentum into it.
        template <typename Mover>
        void Step(f32 dt, f32 gravity_y, Mover&& mover)
        {
            if (!(dt > 0.0f))
            {
                return;
            }

            const bool on_ground = ground_ == PlayerGround::OnGround;
            f32 vertical = velocity_.y;
            if (on_ground && vertical < RisingThreshold)
            {
                vertical = 0.0f;
                if (jump_pending_ && settings_.jump_height > 0.0f)
                {
                    vertical = PlayerJumpSpeed(settings_.jump_height, gravity_y);
                    ++jump_count_;
                }
            }
            jump_pending_ = false;

            const math::Vec3 wish_velocity = PlayerWishDirection(yaw_, forward_, right_) * settings_.move_speed;
            math::Vec3 horizontal{velocity_.x, 0.0f, velocity_.z};
            if (on_ground)
            {
                horizontal = wish_velocity;
            }
            else if (math::Length(wish_velocity) > 0.0f)
            {
                horizontal = MoveTowards(horizontal, wish_velocity, PlayerAirAcceleration * dt);
            }

            const f32 vertical_end = vertical + (gravity_y * dt);
            const f32 vertical_move = 0.5f * (vertical + vertical_end);
            const math::Vec3 requested{horizontal.x, vertical_move, horizontal.z};

            const math::Vec3 before = feet_;
            const PlayerMoveResult result = mover(requested, dt);
            feet_ = result.feet_position;
            ground_ = result.ground;
            ++step_count_;

            const math::Vec3 achieved = (feet_ - before) * (1.0f / dt);
            f32 vertical_after = vertical_end;
            if (vertical_move > 0.0f && achieved.y < vertical_move - BlockedTolerance)
            {
                vertical_after = std::min(vertical_after, std::max(achieved.y, 0.0f));
            }
            else if (vertical_move < 0.0f && achieved.y > vertical_move + BlockedTolerance)
            {
                vertical_after = std::max(vertical_after, std::min(achieved.y, 0.0f));
            }

            const math::Vec3 achieved_horizontal{achieved.x, 0.0f, achieved.z};
            if (math::Length(achieved_horizontal) + BlockedTolerance < math::Length(horizontal))
            {
                horizontal = achieved_horizontal;
            }

            velocity_ = math::Vec3{horizontal.x, vertical_after, horizontal.z};
        }

        // True when the feet are below the fall-reset height while the start is
        // above it. A start at or below the line would reset forever, so in that
        // case the reset is disabled (FallResetDisabled reports it).
        [[nodiscard]] bool NeedsFallReset() const
        {
            return !FallResetDisabled() && feet_.y < settings_.fall_reset_height;
        }

        [[nodiscard]] bool FallResetDisabled() const
        {
            return start_feet_.y <= settings_.fall_reset_height;
        }

        [[nodiscard]] math::Vec3 FeetPosition() const { return feet_; }
        [[nodiscard]] math::Vec3 EyePosition() const
        {
            return feet_ + math::Vec3{0.0f, settings_.eye_height, 0.0f};
        }
        [[nodiscard]] math::Vec3 StartPosition() const { return start_feet_; }
        [[nodiscard]] math::Vec3 Velocity() const { return velocity_; }
        [[nodiscard]] f32 Yaw() const { return yaw_; }
        [[nodiscard]] f32 Pitch() const { return pitch_; }
        [[nodiscard]] PlayerGround Ground() const { return ground_; }
        [[nodiscard]] bool IsGrounded() const { return ground_ == PlayerGround::OnGround; }
        [[nodiscard]] bool JumpPending() const { return jump_pending_; }

        // Diagnostics (and tests): jumps launched and substeps taken so far.
        [[nodiscard]] u64 JumpCount() const { return jump_count_; }
        [[nodiscard]] u64 StepCount() const { return step_count_; }

    private:
        // Upward speed above which the player counts as rising even while the
        // ground contact still reports OnGround right after take-off.
        static constexpr f32 RisingThreshold = 0.1f;

        // Velocity difference (m/s) below which a substep counts as unobstructed.
        static constexpr f32 BlockedTolerance = 0.01f;

        [[nodiscard]] static f32 WrapYaw(f32 yaw)
        {
            return std::isfinite(yaw) ? static_cast<f32>(std::remainder(yaw, 2.0f * math::Pi)) : 0.0f;
        }

        [[nodiscard]] static math::Vec3 MoveTowards(const math::Vec3& from, const math::Vec3& to, f32 max_delta)
        {
            const math::Vec3 difference = to - from;
            const f32 distance = math::Length(difference);
            if (distance <= max_delta || distance <= 0.0f)
            {
                return to;
            }
            return from + (difference * (max_delta / distance));
        }

        PlayerMovementSettings settings_{};
        math::Vec3 feet_{};
        math::Vec3 start_feet_{};
        math::Vec3 velocity_{};
        f32 yaw_ = 0.0f;
        f32 pitch_ = 0.0f;
        PlayerGround ground_ = PlayerGround::Unsupported;
        f32 forward_ = 0.0f;
        f32 right_ = 0.0f;
        bool jump_pending_ = false;
        u64 jump_count_ = 0;
        u64 step_count_ = 0;
    };
}
