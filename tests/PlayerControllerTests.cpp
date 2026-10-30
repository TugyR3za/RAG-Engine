#include "PlayerController.h"
#include "SimulationController.h"

#include <cmath>
#include <iostream>
#include <string>
#include <tuple>

// Unit tests for the engine-free first-person movement rules. Collision is a
// tiny analytic stand-in (an infinite floor, an optional ceiling and an optional
// wall), so these check the rules themselves; tests/PlayerPhysicsTests.cpp
// repeats the important ones against real Jolt geometry.
namespace
{
    using rag::f32;
    using rag::math::Vec3;
    using rag::runtime::PlayerController;
    using rag::runtime::PlayerGround;
    using rag::runtime::PlayerIntent;
    using rag::runtime::PlayerMoveResult;

    constexpr f32 Tick = 1.0f / 60.0f;
    constexpr f32 Gravity = -9.81f;
    constexpr f32 CapsuleHeight = 1.8f;

    int failures = 0;

    bool Expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Player controller test failed: " << message << '\n';
            ++failures;
        }
        return condition;
    }

    bool ExpectNear(f32 actual, f32 expected, f32 tolerance, const std::string& message)
    {
        return Expect(
            std::abs(actual - expected) <= tolerance,
            message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
    }

    f32 HorizontalLength(const Vec3& v)
    {
        return std::sqrt((v.x * v.x) + (v.z * v.z));
    }

    // Analytic stand-in for the character sweep: clamps the feet against the
    // floor, a ceiling (head) and a wall on +X.
    struct FakeWorld
    {
        Vec3 feet{};
        bool has_floor = true;
        f32 floor_y = 0.0f;
        f32 ceiling_y = 1.0e9f;
        f32 wall_x = 1.0e9f;

        PlayerMoveResult Move(const Vec3& velocity, f32 dt)
        {
            Vec3 next = feet + (velocity * dt);
            if (has_floor && next.y < floor_y)
            {
                next.y = floor_y;
            }
            if (next.y + CapsuleHeight > ceiling_y)
            {
                next.y = ceiling_y - CapsuleHeight;
            }
            if (next.x > wall_x)
            {
                next.x = wall_x;
            }
            feet = next;
            const bool grounded = has_floor && feet.y <= floor_y + 0.0001f;
            return PlayerMoveResult{feet, grounded ? PlayerGround::OnGround : PlayerGround::Unsupported};
        }
    };

    struct Rig
    {
        PlayerController player;
        FakeWorld world;

        explicit Rig(const Vec3& start = Vec3{})
        {
            world.feet = start;
            player.SetStartPosition(start);
            player.Reset(start, 0.0f, start.y <= 0.0001f ? PlayerGround::OnGround : PlayerGround::Unsupported);
        }

        // One frame of frame_dt seconds, split exactly as the sandbox does.
        void Frame(const PlayerIntent& intent, f32 frame_dt)
        {
            player.BeginFrame(intent);
            const rag::u32 substeps = rag::runtime::SimulationSubsteps(frame_dt, Tick);
            for (rag::u32 index = 0; index < substeps; ++index)
            {
                player.Step(frame_dt / static_cast<f32>(substeps), Gravity,
                    [this](const Vec3& velocity, f32 dt) { return world.Move(velocity, dt); });
            }
        }

        void Frames(const PlayerIntent& intent, int count, f32 frame_dt = Tick)
        {
            for (int index = 0; index < count; ++index)
            {
                Frame(intent, frame_dt);
            }
        }
    };

    PlayerIntent Move(f32 forward, f32 right)
    {
        PlayerIntent intent;
        intent.forward = forward;
        intent.right = right;
        return intent;
    }

    PlayerIntent JumpPress()
    {
        PlayerIntent intent;
        intent.jump_pressed = true;
        return intent;
    }

    void TestSubsteps()
    {
        using rag::runtime::SimulationSubsteps;
        Expect(SimulationSubsteps(0.0f, Tick) == 0, "no advance means no substeps");
        Expect(SimulationSubsteps(-1.0f, Tick) == 0, "a negative advance means no substeps");
        Expect(SimulationSubsteps(Tick, Tick) == 1, "exactly one tick is one substep");
        Expect(SimulationSubsteps(1.0f / 144.0f, Tick) == 1, "a 144 Hz frame is one substep");
        Expect(SimulationSubsteps(0.033f, Tick) == 2, "the 33 ms frame cap is two substeps");
    }

    void TestWalkSpeedAndDiagonal()
    {
        for (const auto& [forward, right, label] : {
                 std::tuple{1.0f, 0.0f, "W"},
                 std::tuple{0.0f, -1.0f, "A"},
                 std::tuple{1.0f, 1.0f, "W+D"},
                 std::tuple{-1.0f, -1.0f, "S+A"}})
        {
            Rig rig;
            rig.Frames(Move(forward, right), 60);
            const Vec3 feet = rig.player.FeetPosition();
            ExpectNear(HorizontalLength(feet), 4.5f, 0.01f,
                std::string(label) + ": one second of walking covers move_speed metres");
            ExpectNear(HorizontalLength(rig.player.Velocity()), 4.5f, 0.001f,
                std::string(label) + ": ground speed equals move_speed (diagonals normalized)");
            ExpectNear(feet.y, 0.0f, 0.0001f, std::string(label) + ": walking stays on the floor");
        }

        // Yaw 0 looks down -Z (forward) with +X to the right, like the editor camera.
        Rig rig;
        rig.Frames(Move(1.0f, 0.0f), 60);
        Expect(rig.player.FeetPosition().z < -4.4f && std::abs(rig.player.FeetPosition().x) < 0.001f,
            "W at yaw 0 walks toward -Z");
        Rig strafe;
        strafe.Frames(Move(0.0f, 1.0f), 60);
        Expect(strafe.player.FeetPosition().x > 4.4f, "D at yaw 0 strafes toward +X");

        // Out-of-range axes are clamped, not amplified.
        Rig fast;
        fast.Frames(Move(5.0f, 0.0f), 60);
        ExpectNear(HorizontalLength(fast.player.FeetPosition()), 4.5f, 0.01f, "axis values beyond 1 are clamped");

        // Releasing the keys on the ground stops immediately (no sliding).
        rig.Frame(PlayerIntent{}, Tick);
        ExpectNear(HorizontalLength(rig.player.Velocity()), 0.0f, 0.0001f, "no input on the ground stops the player");
    }

    void TestLook()
    {
        Rig rig;
        rig.player.ApplyLook(100.0f, 0.0f); // 100 counts * 0.1 deg
        ExpectNear(rig.player.Yaw(), rag::math::ToRadians(-10.0f), 0.0001f, "mouse right turns right by sensitivity * counts");
        rig.player.ApplyLook(0.0f, -50.0f);
        ExpectNear(rig.player.Pitch(), rag::math::ToRadians(5.0f), 0.0001f, "mouse up looks up");
        rig.player.ApplyLook(0.0f, -100000.0f);
        ExpectNear(rig.player.Pitch(), rag::runtime::PlayerMaxPitchRadians, 0.0001f, "pitch is clamped looking up");
        rig.player.ApplyLook(0.0f, 100000.0f);
        ExpectNear(rig.player.Pitch(), -rag::runtime::PlayerMaxPitchRadians, 0.0001f, "pitch is clamped looking down");
        rig.player.ApplyLook(100000.0f, 0.0f);
        Expect(std::abs(rig.player.Yaw()) <= rag::math::Pi + 0.0001f, "yaw stays wrapped into [-pi, pi]");

        // After turning 90 degrees left, W walks toward -X.
        Rig turned;
        turned.player.ApplyLook(-900.0f, 0.0f);
        turned.Frames(Move(1.0f, 0.0f), 60);
        Expect(turned.player.FeetPosition().x < -4.4f, "W follows the horizontal view direction");
    }

    void TestJumpGating()
    {
        Rig rig;
        rig.Frame(JumpPress(), Tick);
        Expect(rig.player.JumpCount() == 1, "a press on the ground jumps");
        Expect(rig.player.Velocity().y > 4.0f, "the jump launches upward");

        // Holding the key is not a new press: the frames that follow carry no
        // edge, so the player lands and stays down.
        rig.Frames(PlayerIntent{}, 120);
        Expect(rig.player.JumpCount() == 1 && rig.player.IsGrounded(), "holding Jump never relaunches");

        // A press in the air is spent, not buffered until landing.
        rig.Frame(JumpPress(), Tick);
        Expect(rig.player.JumpCount() == 2, "a new press after landing jumps again");
        rig.Frames(PlayerIntent{}, 10);
        rig.Frame(JumpPress(), Tick);
        Expect(rig.player.JumpCount() == 2, "a press while airborne does not jump");
        rig.Frames(PlayerIntent{}, 120);
        Expect(rig.player.JumpCount() == 2 && rig.player.IsGrounded(), "an airborne press is not replayed on landing");

        // A long frame split into substeps still uses the press only once.
        Rig long_frame;
        long_frame.Frame(JumpPress(), 0.033f);
        Expect(long_frame.player.JumpCount() == 1, "one press jumps once even across two substeps");
        Expect(!long_frame.player.JumpPending(), "the press is spent by the first substep");

        // Pressing on consecutive frames right after take-off cannot double-jump,
        // even though the first substep after leaving may still be in contact.
        Rig spam;
        for (int frame = 0; frame < 10; ++frame)
        {
            spam.Frame(JumpPress(), Tick);
        }
        Expect(spam.player.JumpCount() == 1, "repeated presses during the rise do not stack jumps");

        Rig no_jump;
        rag::runtime::PlayerMovementSettings settings;
        settings.jump_height = 0.0f;
        no_jump.player.SetSettings(settings);
        no_jump.Frame(JumpPress(), Tick);
        Expect(no_jump.player.JumpCount() == 0 && no_jump.player.FeetPosition().y <= 0.0001f,
            "jump height 0 disables jumping");
    }

    // The apex depends only on jump_height and gravity, not on the frame rate.
    void TestJumpApexAcrossFrameRates()
    {
        for (const f32 frame_dt : {1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 144.0f, 1.0f / 240.0f})
        {
            Rig rig;
            rig.Frame(JumpPress(), frame_dt);
            f32 apex = rig.player.FeetPosition().y;
            for (int frame = 0; frame < 600 && !rig.player.IsGrounded(); ++frame)
            {
                rig.Frame(PlayerIntent{}, frame_dt);
                apex = std::max(apex, rig.player.FeetPosition().y);
            }
            const std::string rate = std::to_string(static_cast<int>(std::lround(1.0f / frame_dt))) + " Hz";
            ExpectNear(apex, 1.0f, 0.01f, "jump apex at " + rate);
            Expect(rig.player.IsGrounded(), "the jump lands again at " + rate);
        }
    }

    void TestCeilingAndWall()
    {
        // Head clearance of 0.4 m: the rise must stop at the ceiling and the
        // player must start falling at once instead of hovering under it.
        Rig rig;
        rig.world.ceiling_y = CapsuleHeight + 0.4f;
        rig.Frame(JumpPress(), Tick);
        f32 apex = 0.0f;
        int frames_to_land = 0;
        int frames_at_ceiling = 0;
        for (; frames_to_land < 600 && !rig.player.IsGrounded(); ++frames_to_land)
        {
            rig.Frame(PlayerIntent{}, Tick);
            apex = std::max(apex, rig.player.FeetPosition().y);
            // The substep that first touches keeps what it achieved before the
            // contact; the next one is fully blocked and must end the rise.
            if (rig.player.FeetPosition().y >= 0.4f - 0.0001f && ++frames_at_ceiling >= 2)
            {
                Expect(rig.player.Velocity().y <= 0.0f, "a head bump ends the rise");
            }
        }
        Expect(frames_at_ceiling <= 2, "the player does not cling to the ceiling");
        Expect(apex <= 0.4f + 0.0001f, "the ceiling caps the jump");
        Expect(frames_to_land < 50, "a blocked jump comes down sooner than a free one (~54 frames)");

        // Airborne momentum into a wall is cancelled rather than kept.
        Rig wall;
        wall.world.wall_x = 0.5f;
        wall.Frame(Move(0.0f, 1.0f), Tick); // build ground speed toward +X
        PlayerIntent jump_right = Move(0.0f, 1.0f);
        jump_right.jump_pressed = true;
        wall.Frame(jump_right, Tick);
        wall.Frames(PlayerIntent{}, 20);
        Expect(wall.player.FeetPosition().x <= 0.5f, "the wall stops the player");
        ExpectNear(wall.player.Velocity().x, 0.0f, 0.02f, "momentum into the wall is cancelled");
    }

    void TestAirControl()
    {
        // Walking off a ledge (no floor) keeps the walking momentum once the
        // keys are released in the air. (Releasing them while still on the
        // ground stops the player before it leaves.)
        Rig rig;
        rig.Frame(Move(1.0f, 0.0f), Tick);
        rig.world.has_floor = false;
        rig.Frame(Move(1.0f, 0.0f), Tick);
        Expect(!rig.player.IsGrounded(), "the player is airborne past the ledge");
        rig.Frames(PlayerIntent{}, 30);
        ExpectNear(HorizontalLength(rig.player.Velocity()), 4.5f, 0.001f, "no input in the air keeps horizontal momentum");
        Expect(rig.player.Velocity().y < -4.0f, "gravity accelerates the fall");

        // Steering in the air is limited by PlayerAirAcceleration.
        rig.Frame(Move(-1.0f, 0.0f), Tick);
        ExpectNear(HorizontalLength(rig.player.Velocity()), 4.5f - (rag::runtime::PlayerAirAcceleration * Tick),
            0.001f, "air steering changes velocity by at most air acceleration * dt");
    }

    void TestFallReset()
    {
        Rig rig;
        rag::runtime::PlayerMovementSettings settings;
        settings.fall_reset_height = -10.0f;
        rig.player.SetSettings(settings);
        rig.world.has_floor = false;

        int frames = 0;
        while (!rig.player.NeedsFallReset() && frames < 600)
        {
            rig.Frame(Move(1.0f, 1.0f), Tick);
            ++frames;
        }
        Expect(rig.player.NeedsFallReset(), "falling below the reset height asks for a reset");
        Expect(rig.player.FeetPosition().y < -10.0f && rig.player.FeetPosition().y > -10.5f,
            "the reset is detected on the substep that crosses the line, not later");

        rig.player.ApplyLook(0.0f, 300.0f);
        rig.player.BeginFrame(JumpPress());
        rig.world.feet = rig.player.StartPosition();
        rig.player.Reset(rig.player.StartPosition(), 0.5f, PlayerGround::Unsupported);
        Expect(!rig.player.NeedsFallReset(), "after the reset the player is back above the line");
        ExpectNear(rag::math::Length(rig.player.Velocity()), 0.0f, 0.0f, "the reset clears all motion");
        ExpectNear(rig.player.Pitch(), 0.0f, 0.0f, "the reset levels the view");
        ExpectNear(rig.player.Yaw(), 0.5f, 0.0001f, "the reset faces the start's yaw");
        Expect(!rig.player.JumpPending(), "the reset drops a pending jump");

        Rig below;
        below.player.SetSettings(settings);
        below.player.SetStartPosition(Vec3{0.0f, -20.0f, 0.0f});
        below.player.Reset(Vec3{0.0f, -20.0f, 0.0f}, 0.0f, PlayerGround::Unsupported);
        Expect(below.player.FallResetDisabled() && !below.player.NeedsFallReset(),
            "a start below the reset height disables the reset instead of looping");
    }

    // The lifecycle decides how much time passes; the player only moves when it
    // does. Paused frames advance nothing even with intent latched, Step is
    // exactly one tick, and a long hitch is clamped before it reaches the player.
    void TestLifecycleGating()
    {
        struct Snapshot
        {
        };
        rag::runtime::SimulationController<Snapshot> simulation(Tick, 0.033f);
        std::string error;
        (void)simulation.Play([](Snapshot&, std::string&) { return true; }, error);

        Rig rig;
        const auto frame = [&](rag::f64 wall_dt, const PlayerIntent& intent)
        {
            const f32 advance = simulation.AdvanceFrame(wall_dt);
            rig.player.BeginFrame(intent);
            const rag::u32 substeps = rag::runtime::SimulationSubsteps(advance, Tick);
            for (rag::u32 index = 0; index < substeps; ++index)
            {
                rig.player.Step(advance / static_cast<f32>(substeps), Gravity,
                    [&rig](const Vec3& velocity, f32 dt) { return rig.world.Move(velocity, dt); });
            }
            return substeps;
        };

        frame(Tick, Move(1.0f, 0.0f));
        (void)simulation.Pause(error);
        const Vec3 paused_at = rig.player.FeetPosition();
        const rag::u64 paused_steps = rig.player.StepCount();
        for (int index = 0; index < 30; ++index)
        {
            frame(Tick, Move(1.0f, 1.0f));
        }
        Expect(rig.player.StepCount() == paused_steps, "no substep runs while Paused");
        Expect(rag::math::Length(rig.player.FeetPosition() - paused_at) == 0.0f, "the player does not move while Paused");

        (void)simulation.RequestStep(error);
        Expect(frame(0.25, PlayerIntent{}) == 1, "Step advances exactly one substep");
        Expect(rig.player.StepCount() == paused_steps + 1, "Step runs the player once");

        (void)simulation.Resume(error);
        frame(Tick, PlayerIntent{});
        Expect(frame(0.5, PlayerIntent{}) == 2, "a 500 ms hitch is clamped to the 33 ms cap (two substeps)");
    }
}

int main()
{
    TestSubsteps();
    TestWalkSpeedAndDiagonal();
    TestLook();
    TestJumpGating();
    TestJumpApexAcrossFrameRates();
    TestCeilingAndWall();
    TestAirControl();
    TestFallReset();
    TestLifecycleGating();

    if (failures != 0)
    {
        std::cerr << failures << " player controller check(s) failed.\n";
        return 1;
    }
    std::cout << "Player controller tests passed.\n";
    return 0;
}
