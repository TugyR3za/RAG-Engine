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
            world.SetDynamicBodyScale(box, {2.0f, 3.0f, 2.0f}),
            "Box scale replacement should succeed.") &&
        Expect(
            world.SetDynamicBodyScale(sphere, {1.0f, 2.0f, 3.0f}),
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
            world.SetDynamicBodyScale(box, {2.0f, 2.0f, 2.0f}),
            "Second box scale replacement should succeed.") &&
        passed;
    world.ResetDynamicBody(box, {-2.0f, 6.0f, 0.0f}, {});
    Simulate(world, 360);
    passed =
        ExpectNear(
            world.GetBodyState(box).position.y,
            1.0f,
            0.08f,
            "Repeated edits must derive collider size from the immutable base shape.") &&
        passed;

    world.Shutdown();
    if (!passed)
    {
        return 1;
    }

    std::cout << "Physics collider scale tests passed.\n";
    return 0;
}
