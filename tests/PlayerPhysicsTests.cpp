#include "PlayerController.h"
#include "SimulationController.h"

#include "Rag/Physics/PhysicsWorld.h"
#include "Rag/Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

// Integration tests: the first-person movement rules driving a real Jolt
// CharacterVirtual through PhysicsWorld, against static and dynamic colliders,
// interleaved with PhysicsWorld::Step exactly as the sandbox does it.
namespace
{
    using rag::f32;
    using rag::math::Vec3;
    using rag::physics::BodyHandle;
    using rag::physics::CharacterHandle;
    using rag::physics::PhysicsWorld;
    using rag::runtime::PlayerController;
    using rag::runtime::PlayerGround;
    using rag::runtime::PlayerIntent;

    constexpr f32 Tick = 1.0f / 60.0f;
    constexpr f32 Gravity = -9.81f;
    constexpr f32 Radius = 0.3f;
    constexpr f32 Height = 1.8f;

    int failures = 0;

    bool Expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Player physics test failed: " << message << '\n';
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

    PlayerGround ToPlayerGround(rag::physics::CharacterGround ground)
    {
        switch (ground)
        {
            case rag::physics::CharacterGround::OnGround: return PlayerGround::OnGround;
            case rag::physics::CharacterGround::OnSteepGround: return PlayerGround::OnSteepGround;
            case rag::physics::CharacterGround::NotSupported:
            case rag::physics::CharacterGround::InAir: return PlayerGround::Unsupported;
        }
        return PlayerGround::Unsupported;
    }

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

    // A fresh world with a large static floor whose top is at y = 0.
    struct World
    {
        PhysicsWorld physics;

        World()
        {
            rag::physics::PhysicsWorldDesc desc{};
            desc.fixed_timestep_seconds = Tick;
            (void)physics.Initialize(desc);
            (void)physics.CreateStaticBox({0.0f, -0.5f, 0.0f}, {50.0f, 0.5f, 50.0f});
        }
    };

    struct Rig
    {
        PhysicsWorld& world;
        CharacterHandle character{};
        PlayerController player;

        Rig(PhysicsWorld& physics_world, const Vec3& start)
            : world(physics_world)
        {
            rag::physics::CharacterDesc desc;
            desc.feet_position = start;
            desc.radius = Radius;
            desc.height = Height;
            character = world.CreateCharacter(desc);
            player.SetStartPosition(start);
            player.Reset(start, 0.0f, ToPlayerGround(world.GetCharacterState(character).ground));
        }

        ~Rig()
        {
            world.DestroyCharacter(character);
        }

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        void Frame(const PlayerIntent& intent, f32 frame_dt = Tick)
        {
            player.BeginFrame(intent);
            const rag::u32 substeps = rag::runtime::SimulationSubsteps(frame_dt, Tick);
            const f32 sub_dt = frame_dt / static_cast<f32>(substeps);
            for (rag::u32 index = 0; index < substeps; ++index)
            {
                player.Step(sub_dt, Gravity, [this](const Vec3& velocity, f32 dt)
                {
                    const rag::physics::CharacterState state = world.MoveCharacter(character, velocity, dt, Gravity);
                    return rag::runtime::PlayerMoveResult{state.feet_position, ToPlayerGround(state.ground)};
                });
                world.Step(sub_dt);
            }
        }

        // The capsule never ends a frame inside a collider.
        [[nodiscard]] bool Clear() const
        {
            return world.IsCharacterSpaceFree(Radius, Height, player.FeetPosition());
        }
    };

    void TestLifetime()
    {
        World w;
        const rag::u32 bodies = w.physics.BodyCount();
        rag::physics::CharacterDesc bad;
        bad.radius = 0.5f;
        bad.height = 0.9f;
        Expect(!w.physics.CreateCharacter(bad).IsValid(), "a capsule shorter than its diameter is refused");

        for (int cycle = 0; cycle < 100; ++cycle)
        {
            rag::physics::CharacterDesc desc;
            desc.feet_position = {0.0f, 0.0f, 0.0f};
            const CharacterHandle character = w.physics.CreateCharacter(desc);
            if (!Expect(character.IsValid() && w.physics.CharacterCount() == 1, "a character is created"))
            {
                return;
            }
            (void)w.physics.MoveCharacter(character, {1.0f, 0.0f, 0.0f}, Tick, Gravity);
            w.physics.DestroyCharacter(character);
            w.physics.DestroyCharacter(character); // stale handle: no-op
        }
        w.physics.DestroyCharacter(CharacterHandle{});
        Expect(w.physics.CharacterCount() == 0, "100 create/destroy cycles leave no character behind");
        Expect(w.physics.BodyCount() == bodies, "characters never add or leak rigid bodies");

        rag::physics::CharacterDesc desc;
        (void)w.physics.CreateCharacter(desc);
        w.physics.Shutdown();
        Expect(w.physics.CharacterCount() == 0, "Shutdown frees remaining characters");
    }

    void TestSpawnProbe()
    {
        World w;
        (void)w.physics.CreateStaticBox({5.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
        Expect(w.physics.IsCharacterSpaceFree(Radius, Height, {0.0f, 0.0f, 0.0f}), "standing on the floor is free");
        Expect(w.physics.IsCharacterSpaceFree(Radius, Height, {0.0f, 1.0f, 0.0f}), "floating above the floor is free");
        Expect(!w.physics.IsCharacterSpaceFree(Radius, Height, {0.0f, -0.1f, 0.0f}), "sunk into the floor is not free");
        Expect(!w.physics.IsCharacterSpaceFree(Radius, Height, {5.0f, 0.5f, 0.0f}), "inside a box is not free");
        Expect(w.physics.IsCharacterSpaceFree(Radius, Height, {5.0f, 2.0f, 0.0f}), "on top of the box is free");
        Expect(!w.physics.IsCharacterSpaceFree(0.5f, 0.9f, {0.0f, 2.0f, 0.0f}), "a degenerate capsule is never free");
    }

    void TestWalkOnFloor()
    {
        World w;
        Rig rig(w.physics, {0.0f, 0.0f, 0.0f});
        Expect(rig.player.IsGrounded(), "a character spawned on the floor starts on the ground");
        bool stayed_down = true;
        for (int frame = 0; frame < 60; ++frame)
        {
            rig.Frame(Move(1.0f, 1.0f));
            stayed_down = stayed_down && std::abs(rig.player.FeetPosition().y) < 0.05f && rig.player.IsGrounded();
        }
        const Vec3 feet = rig.player.FeetPosition();
        ExpectNear(std::sqrt((feet.x * feet.x) + (feet.z * feet.z)), 4.5f, 0.1f,
            "one second of diagonal walking covers move_speed metres on real ground");
        Expect(stayed_down, "walking keeps the feet on the floor and grounded");
        Expect(rig.Clear(), "walking never sinks into the floor");
    }

    void TestWall()
    {
        World w;
        // Wall face at z = -2.
        (void)w.physics.CreateStaticBox({0.0f, 1.5f, -2.5f}, {5.0f, 1.5f, 0.5f});
        Rig rig(w.physics, {0.0f, 0.0f, 0.0f});
        bool never_inside = true;
        for (int frame = 0; frame < 120; ++frame)
        {
            rig.Frame(Move(1.0f, 0.0f));
            never_inside = never_inside && rig.player.FeetPosition().z - Radius >= -2.0f - 0.01f && rig.Clear();
        }
        Expect(never_inside, "walking into a wall never passes into it");
        ExpectNear(rig.player.FeetPosition().z, -2.0f + Radius, 0.05f, "the player stops against the wall face");
    }

    void TestLedgeFallAndLand()
    {
        World w;
        // A platform with its top at y = 2, edge at z = -2.
        (void)w.physics.CreateStaticBox({0.0f, 1.0f, 0.0f}, {2.0f, 1.0f, 2.0f});
        Rig rig(w.physics, {0.0f, 2.0f, 0.0f});
        bool left_ground = false;
        for (int frame = 0; frame < 180; ++frame)
        {
            rig.Frame(Move(frame < 40 ? 1.0f : 0.0f, 0.0f));
            left_ground = left_ground || !rig.player.IsGrounded();
        }
        Expect(left_ground, "walking off the edge leaves the ground");
        ExpectNear(rig.player.FeetPosition().y, 0.0f, 0.05f, "the fall lands on the floor below");
        Expect(rig.player.IsGrounded() && rig.Clear(), "the landing is grounded and clear of the floor");
        Expect(rig.player.FeetPosition().z < -2.0f - Radius, "airborne momentum carried the player clear of the edge");
    }

    void TestCeiling()
    {
        World w;
        // Ceiling slab bottom at y = 2.3: 0.5 m above a standing head.
        (void)w.physics.CreateStaticBox({0.0f, 2.8f, 0.0f}, {3.0f, 0.5f, 3.0f});
        Rig rig(w.physics, {0.0f, 0.0f, 0.0f});
        rag::runtime::PlayerMovementSettings settings;
        settings.jump_height = 1.5f;
        rig.player.SetSettings(settings);
        rig.Frame(JumpPress());
        f32 apex = 0.0f;
        int frames = 0;
        for (; frames < 300 && !(frames > 5 && rig.player.IsGrounded()); ++frames)
        {
            rig.Frame(PlayerIntent{});
            apex = std::max(apex, rig.player.FeetPosition().y);
            Expect(rig.Clear(), "the head never enters the ceiling");
        }
        Expect(rig.player.JumpCount() == 1, "the jump launched");
        Expect(apex <= 0.5f + 0.03f && apex > 0.3f, "the ceiling caps the jump (apex " + std::to_string(apex) + ")");
        Expect(rig.player.IsGrounded() && frames < 60, "after the head bump the player falls straight back down");
    }

    // Dynamic bodies get Jolt's default density (1000 kg/m^3), so a 0.5 m crate
    // weighs 125 kg and slides under the default 600 N push, while a 1 m crate
    // (1 t, like the default scene's cubes) does not budge -- but in both cases
    // the player must stay outside it.
    void TestDynamicProps()
    {
        for (const f32 half : {0.25f, 0.5f})
        {
            World w;
            const BodyHandle crate = w.physics.CreateDynamicBox({0.0f, half, -2.0f}, {half, half, half});
            for (int frame = 0; frame < 30; ++frame)
            {
                w.physics.Step(Tick); // settle
            }
            const Vec3 crate_start = w.physics.GetBodyState(crate).position;
            Rig rig(w.physics, {0.0f, 0.0f, 0.0f});
            bool never_inside = true;
            for (int frame = 0; frame < 180; ++frame)
            {
                rig.Frame(Move(1.0f, 0.0f));
                never_inside = never_inside && rig.Clear();
            }
            const Vec3 crate_end = w.physics.GetBodyState(crate).position;
            const f32 pushed = crate_start.z - crate_end.z;
            const std::string label = std::to_string(static_cast<int>(std::lround(half * 200.0f))) + " cm crate";
            Expect(never_inside, label + ": walking into it never passes into it");
            Expect(rig.player.FeetPosition().z > crate_end.z, label + ": the player stays on its near side");
            if (half < 0.3f)
            {
                Expect(pushed > 0.5f, label + ": the player pushes it (moved " + std::to_string(pushed) + " m)");
            }
            else
            {
                Expect(std::abs(pushed) < 0.1f, label + ": a 1 t crate blocks the player (moved " +
                    std::to_string(pushed) + " m)");
            }
        }
    }

    void TestSlopes()
    {
        // 30 degrees is walkable; 60 degrees is steeper than the 50 degree limit.
        for (const f32 degrees : {30.0f, 60.0f})
        {
            World w;
            rag::physics::BodyDesc ramp;
            ramp.shape = rag::physics::ColliderShape::Box;
            ramp.mode = rag::physics::BodyMode::Static;
            ramp.box_half_extents = {2.0f, 0.1f, 6.0f};
            // Tilted about X so it rises toward -Z, starting at the player's feet.
            const f32 angle = rag::math::ToRadians(degrees);
            ramp.rotation = rag::math::QuatFromEulerXYZ({angle, 0.0f, 0.0f});
            ramp.position = {0.0f, 6.0f * std::sin(angle) - 0.1f, -1.0f - 6.0f * std::cos(angle)};
            (void)w.physics.CreateBody(ramp);

            Rig rig(w.physics, {0.0f, 0.0f, 0.0f});
            bool reached_steep = false;
            for (int frame = 0; frame < 120; ++frame)
            {
                rig.Frame(Move(1.0f, 0.0f));
                reached_steep = reached_steep || rig.player.Ground() == PlayerGround::OnSteepGround;
            }
            const std::string label = std::to_string(static_cast<int>(degrees)) + " degree slope";
            if (degrees < 50.0f)
            {
                Expect(rig.player.FeetPosition().y > 1.5f, label + ": the player walks up it");
            }
            else
            {
                Expect(rig.player.FeetPosition().y < 0.5f, label + ": the player cannot walk up it (y=" +
                    std::to_string(rig.player.FeetPosition().y) + ")");
            }
            Expect(rig.Clear(), label + ": the capsule stays out of the ramp");
            (void)reached_steep;
        }
    }

    // The same walk and jump at different frame rates end in the same place.
    void TestFrameRates()
    {
        f32 reference_distance = 0.0f;
        f32 reference_apex = 0.0f;
        for (const f32 frame_dt : {1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 144.0f})
        {
            World w;
            Rig rig(w.physics, {0.0f, 0.0f, 0.0f});
            const int frames = static_cast<int>(std::lround(1.0f / frame_dt));
            for (int frame = 0; frame < frames; ++frame)
            {
                rig.Frame(Move(1.0f, 0.0f), frame_dt);
            }
            const f32 distance = -rig.player.FeetPosition().z;

            rig.Frame(JumpPress(), frame_dt);
            f32 apex = 0.0f;
            for (int frame = 0; frame < 4 * frames && !(frame > 2 && rig.player.IsGrounded()); ++frame)
            {
                rig.Frame(PlayerIntent{}, frame_dt);
                apex = std::max(apex, rig.player.FeetPosition().y);
            }

            const std::string rate = std::to_string(static_cast<int>(std::lround(1.0f / frame_dt))) + " Hz";
            if (reference_distance == 0.0f)
            {
                reference_distance = distance;
                reference_apex = apex;
                ExpectNear(apex, 1.0f, 0.05f, "jump apex on real ground at " + rate);
            }
            else
            {
                ExpectNear(distance, reference_distance, 0.05f, "walk distance at " + rate + " matches 60 Hz");
                ExpectNear(apex, reference_apex, 0.05f, "jump apex at " + rate + " matches 60 Hz");
            }
            Expect(rig.player.IsGrounded(), "the jump lands at " + rate);
        }
    }

    // A long frame is clamped by the lifecycle before it reaches the player, and
    // even a fast fall onto a thin slab does not tunnel through it.
    void TestLongFrameAndFastFall()
    {
        World w;
        // A 10 cm slab with its top at y = 5, high above the floor.
        (void)w.physics.CreateStaticBox({0.0f, 4.95f, 0.0f}, {3.0f, 0.05f, 3.0f});
        Rig rig(w.physics, {0.0f, 40.0f, 0.0f});

        struct Snapshot
        {
        };
        rag::runtime::SimulationController<Snapshot> simulation(Tick, 0.033f);
        std::string error;
        (void)simulation.Play([](Snapshot&, std::string&) { return true; }, error);
        for (int frame = 0; frame < 300; ++frame)
        {
            // Every fifth frame is a 400 ms hitch.
            const f32 advance = simulation.AdvanceFrame(frame % 5 == 4 ? 0.4 : 0.033);
            Expect(advance <= 0.033f + 0.0001f, "the lifecycle clamps every frame to the 33 ms cap");
            rig.Frame(PlayerIntent{}, advance);
        }
        ExpectNear(rig.player.FeetPosition().y, 5.0f, 0.05f, "a 35 m fall lands on a 10 cm slab without tunnelling");
        Expect(rig.player.IsGrounded(), "the fast fall ends grounded");
    }

    void TestFallResetTeleport()
    {
        World w;
        Rig rig(w.physics, {0.0f, 0.0f, 0.0f});
        rag::runtime::PlayerMovementSettings settings;
        settings.fall_reset_height = -5.0f;
        rig.player.SetSettings(settings);

        // Walk off the edge of the 100 m floor... faster: teleport past it.
        w.physics.SetCharacterPosition(rig.character, {60.0f, 0.0f, 0.0f});
        rig.player.Reset({60.0f, 0.0f, 0.0f}, 0.0f, PlayerGround::Unsupported);
        int frames = 0;
        while (!rig.player.NeedsFallReset() && frames < 300)
        {
            rig.Frame(Move(1.0f, 0.0f));
            ++frames;
        }
        Expect(rig.player.NeedsFallReset(), "falling off the world asks for a reset");

        const rag::u32 bodies = w.physics.BodyCount();
        const Vec3 start = rig.player.StartPosition();
        w.physics.SetCharacterPosition(rig.character, start);
        const rag::physics::CharacterState state = w.physics.GetCharacterState(rig.character);
        rig.player.Reset(state.feet_position, 0.0f, ToPlayerGround(state.ground));
        Expect(rag::math::Length(rig.player.FeetPosition() - start) < 0.001f, "the reset teleports to the start");
        Expect(rag::math::Length(rig.player.Velocity()) == 0.0f, "the reset clears motion");
        Expect(rig.player.IsGrounded(), "the reset start reports ground immediately");
        rig.Frame(PlayerIntent{});
        Expect(std::abs(rig.player.FeetPosition().y) < 0.05f, "after the reset the player stands still at the start");
        Expect(w.physics.BodyCount() == bodies, "the reset touches no rigid body");
    }

    void TestDefaultsMatchAuthoring()
    {
        const rag::scene::PlayerStartComponent authored{};
        const rag::runtime::PlayerMovementSettings runtime{};
        Expect(authored.move_speed == runtime.move_speed &&
               authored.mouse_sensitivity_degrees == runtime.mouse_sensitivity_degrees &&
               authored.jump_height == runtime.jump_height &&
               authored.eye_height == runtime.eye_height &&
               authored.fall_reset_height == runtime.fall_reset_height,
            "runtime movement defaults match the Player Start component defaults");
        const rag::physics::CharacterDesc desc{};
        Expect(desc.radius == authored.capsule_radius && desc.height == authored.capsule_height,
            "the physics capsule defaults match the Player Start component defaults");
    }
}

int main()
{
    TestDefaultsMatchAuthoring();
    TestLifetime();
    TestSpawnProbe();
    TestWalkOnFloor();
    TestWall();
    TestLedgeFallAndLand();
    TestCeiling();
    TestDynamicProps();
    TestSlopes();
    TestFrameRates();
    TestLongFrameAndFastFall();
    TestFallResetTeleport();

    if (failures != 0)
    {
        std::cerr << failures << " player physics check(s) failed.\n";
        return 1;
    }
    std::cout << "Player physics tests passed.\n";
    return 0;
}
