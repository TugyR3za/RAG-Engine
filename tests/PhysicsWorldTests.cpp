#include "Rag/Physics/PhysicsWorld.h"

#include <cmath>
#include <iostream>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Physics test failed: " << message << '\n';
        }
        return condition;
    }

    bool ExpectNear(rag::f32 actual, rag::f32 expected, rag::f32 tolerance, const char* message)
    {
        if (std::abs(actual - expected) > tolerance)
        {
            std::cerr
                << "Physics test failed: "
                << message
                << " (actual="
                << actual
                << ", expected="
                << expected
                << ")\n";
            return false;
        }
        return true;
    }

    void Simulate(rag::physics::PhysicsWorld& world, int steps)
    {
        for (int index = 0; index < steps; ++index)
        {
            world.Step(1.0f / 60.0f);
        }
    }
}

int main()
{
    rag::physics::PhysicsWorld world;
    rag::physics::PhysicsWorldDesc desc{};
    if (!Expect(world.Initialize(desc), "PhysicsWorld should initialize."))
    {
        return 1;
    }

    const rag::physics::BodyHandle ground =
        world.CreateStaticBox({0.0f, -0.5f, 0.0f}, {10.0f, 0.5f, 10.0f});
    const rag::physics::BodyHandle box =
        world.CreateDynamicBox({-2.0f, 6.0f, 0.0f}, {0.5f, 0.5f, 0.5f});
    const rag::physics::BodyHandle sphere =
        world.CreateDynamicSphere({2.0f, 7.0f, 0.0f}, 0.5f);

    bool passed =
        Expect(ground.IsValid(), "Ground body should be valid.") &&
        Expect(box.IsValid(), "Box body should be valid.") &&
        Expect(sphere.IsValid(), "Sphere body should be valid.");

    passed =
        Expect(
            world.SetBodyScale(box, {2.0f, 3.0f, 2.0f}),
            "Box scale replacement should succeed.") &&
        Expect(
            world.SetBodyScale(sphere, {1.0f, 2.0f, 3.0f}),
            "Sphere scale replacement should succeed.") &&
        passed;

    Simulate(world, 360);
    passed =
        ExpectNear(
            world.GetBodyState(box).position.y,
            1.5f,
            0.08f,
            "A box scaled 3x on Y should rest with its center 1.5 units above the floor.") &&
        ExpectNear(
            world.GetBodyState(sphere).position.y,
            1.5f,
            0.08f,
            "A non-uniform sphere scale should use the largest axis as a uniform radius.") &&
        passed;

    // Rescale the same box from its original 0.5 half-extent. If scaling were
    // compounded from the current collider, this would rest far above y=1.
    passed =
        Expect(
            world.SetBodyScale(box, {2.0f, 2.0f, 2.0f}),
            "Second box scale replacement should succeed.") &&
        passed;
    world.SetBodyPose(box, {-2.0f, 6.0f, 0.0f}, {});
    Simulate(world, 360);
    passed =
        ExpectNear(
            world.GetBodyState(box).position.y,
            1.0f,
            0.08f,
            "Repeated edits must derive collider size from the immutable base shape.") &&
        passed;

    // A static body created through the full descriptor must never fall, even
    // when spawned high in the air, and must sit exactly where it was placed.
    rag::physics::BodyDesc static_desc;
    static_desc.shape = rag::physics::ColliderShape::Box;
    static_desc.box_half_extents = {0.5f, 0.5f, 0.5f};
    static_desc.position = {5.0f, 5.0f, 0.0f};
    static_desc.mode = rag::physics::BodyMode::Static;
    const rag::physics::BodyHandle static_box = world.CreateBody(static_desc);

    // A dynamic body with gravity disabled should also hold its height (it stays
    // put but can still collide / be pushed).
    rag::physics::BodyDesc floater_desc;
    floater_desc.shape = rag::physics::ColliderShape::Box;
    floater_desc.box_half_extents = {0.5f, 0.5f, 0.5f};
    floater_desc.position = {8.0f, 5.0f, 0.0f};
    floater_desc.mode = rag::physics::BodyMode::Dynamic;
    floater_desc.gravity_enabled = false;
    const rag::physics::BodyHandle floater = world.CreateBody(floater_desc);

    passed =
        Expect(static_box.IsValid(), "Static descriptor body should be valid.") &&
        Expect(floater.IsValid(), "Gravity-disabled body should be valid.") &&
        passed;

    Simulate(world, 180);
    passed =
        ExpectNear(
            world.GetBodyState(static_box).position.y,
            5.0f,
            0.001f,
            "A static body must not move under gravity.") &&
        ExpectNear(
            world.GetBodyState(floater).position.y,
            5.0f,
            0.05f,
            "A dynamic body with gravity disabled must hold its height.") &&
        passed;

    // Re-enabling gravity must let the floater fall; destroying a body must make
    // it untracked (both exercise the live-reconfiguration paths the editor uses).
    world.SetBodyGravityEnabled(floater, true);
    Simulate(world, 180);
    passed =
        Expect(
            world.GetBodyState(floater).position.y < 4.0f,
            "Enabling gravity must let a dynamic body fall.") &&
        passed;

    world.DestroyBody(static_box);
    passed =
        Expect(
            !world.SetBodyScale(static_box, {2.0f, 2.0f, 2.0f}),
            "Scaling a destroyed body must fail (record removed).") &&
        passed;

    // An offset box collider (used for off-centre-pivot meshes like the duck) must
    // rest on its offset face, not its pivot: half-extents 0.5 pushed up by 1.0 put
    // the collider bottom 0.5 above the pivot, so a drop rests the pivot at y=-0.5.
    rag::physics::BodyDesc offset_desc;
    offset_desc.shape = rag::physics::ColliderShape::Box;
    offset_desc.box_half_extents = {0.5f, 0.5f, 0.5f};
    offset_desc.offset = {0.0f, 1.0f, 0.0f};
    offset_desc.position = {-5.0f, 5.0f, 0.0f};
    offset_desc.mode = rag::physics::BodyMode::Dynamic;
    const rag::physics::BodyHandle offset_box = world.CreateBody(offset_desc);
    passed = Expect(offset_box.IsValid(), "Offset box body should be valid.") && passed;
    Simulate(world, 360);
    passed =
        ExpectNear(
            world.GetBodyState(offset_box).position.y,
            -0.5f,
            0.08f,
            "An offset box must rest on its offset collider face, not its pivot.") &&
        passed;

    world.Shutdown();
    if (!passed)
    {
        return 1;
    }

    std::cout << "Physics collider scale tests passed.\n";
    return 0;
}
