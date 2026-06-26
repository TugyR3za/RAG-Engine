#include "Rag/Physics/PhysicsWorld.h"

#include "Rag/Core/Log.h"

// Jolt requires <Jolt/Jolt.h> to be the first Jolt header included. Its headers
// are intentionally noisy under /W4 + /permissive-, so the whole Jolt include
// block is wrapped in a warning silence on MSVC.
#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace rag::physics
{
    namespace
    {
        // The two required object layers: static and dynamic.
        namespace Layers
        {
            static constexpr JPH::ObjectLayer NON_MOVING = 0;
            static constexpr JPH::ObjectLayer MOVING = 1;
            static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
        }

        // Matching broad-phase layers.
        namespace BroadPhaseLayers
        {
            static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
            static constexpr JPH::BroadPhaseLayer MOVING(1);
            static constexpr JPH::uint NUM_LAYERS = 2;
        }

        // Maps each object layer to a broad-phase layer.
        class BroadPhaseLayerImpl final : public JPH::BroadPhaseLayerInterface
        {
        public:
            BroadPhaseLayerImpl()
            {
                object_to_broad_phase_[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
                object_to_broad_phase_[Layers::MOVING] = BroadPhaseLayers::MOVING;
            }

            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return BroadPhaseLayers::NUM_LAYERS;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return object_to_broad_phase_[layer];
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                if (layer == BroadPhaseLayers::NON_MOVING)
                {
                    return "NON_MOVING";
                }
                if (layer == BroadPhaseLayers::MOVING)
                {
                    return "MOVING";
                }
                return "INVALID";
            }
#endif

        private:
            JPH::BroadPhaseLayer object_to_broad_phase_[Layers::NUM_LAYERS];
        };

        // Decides whether an object layer can collide with a broad-phase layer.
        class ObjectVsBroadPhaseFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override
            {
                switch (layer1)
                {
                case Layers::NON_MOVING:
                    return layer2 == BroadPhaseLayers::MOVING;
                case Layers::MOVING:
                    return true;
                default:
                    return false;
                }
            }
        };

        // Decides whether two object layers can collide.
        class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override
            {
                switch (layer1)
                {
                case Layers::NON_MOVING:
                    return layer2 == Layers::MOVING; // static collides only with dynamic
                case Layers::MOVING:
                    return true; // dynamic collides with everything
                default:
                    return false;
                }
            }
        };

        void JoltTrace(const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            char buffer[1024];
            std::vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            RAG_LOG_TRACE("[Jolt] ", buffer);
        }

#ifdef JPH_ENABLE_ASSERTS
        bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
        {
            RAG_LOG_ERROR(
                "[Jolt] Assert failed: ",
                file,
                ":",
                line,
                ": (",
                expression,
                ") ",
                message != nullptr ? message : "");
            return true; // break into the debugger
        }
#endif

        // Adds a body to the simulation and records its id for later cleanup.
        JPH::BodyID AddShapeBody(
            JPH::BodyInterface& body_interface,
            std::vector<JPH::BodyID>& tracked_bodies,
            const JPH::Shape* shape,
            const math::Vec3& position,
            JPH::EMotionType motion_type,
            JPH::ObjectLayer layer,
            JPH::EActivation activation)
        {
            JPH::BodyCreationSettings settings(
                shape,
                JPH::RVec3(position.x, position.y, position.z),
                JPH::Quat::sIdentity(),
                motion_type,
                layer);

            const JPH::BodyID id = body_interface.CreateAndAddBody(settings, activation);
            if (!id.IsInvalid())
            {
                tracked_bodies.push_back(id);
            }
            return id;
        }
    }

    struct PhysicsWorld::Impl
    {
        PhysicsWorldDesc desc{};
        bool initialized = false;

        std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
        std::unique_ptr<JPH::JobSystemThreadPool> job_system;
        BroadPhaseLayerImpl broad_phase_layer_interface;
        ObjectVsBroadPhaseFilterImpl object_vs_broad_phase_filter;
        ObjectLayerPairFilterImpl object_layer_pair_filter;
        JPH::PhysicsSystem physics_system;
        std::vector<JPH::BodyID> bodies;
    };

    PhysicsWorld::PhysicsWorld() = default;

    PhysicsWorld::~PhysicsWorld()
    {
        Shutdown();
    }

    bool PhysicsWorld::Initialize(const PhysicsWorldDesc& desc)
    {
        if (impl_ && impl_->initialized)
        {
            return true;
        }

        impl_ = std::make_unique<Impl>();
        impl_->desc = desc;

        JPH::RegisterDefaultAllocator();

        JPH::Trace = JoltTrace;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)

        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        // 16 MiB scratch buffer for a frame's worth of temporary allocations.
        impl_->temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);

        const unsigned int hardware_threads = std::thread::hardware_concurrency();
        const int worker_threads = static_cast<int>(hardware_threads > 1 ? hardware_threads - 1 : 1);
        impl_->job_system = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            worker_threads);

        constexpr JPH::uint max_bodies = 1024;
        constexpr JPH::uint num_body_mutexes = 0; // 0 = let Jolt pick a default
        constexpr JPH::uint max_body_pairs = 1024;
        constexpr JPH::uint max_contact_constraints = 1024;

        impl_->physics_system.Init(
            max_bodies,
            num_body_mutexes,
            max_body_pairs,
            max_contact_constraints,
            impl_->broad_phase_layer_interface,
            impl_->object_vs_broad_phase_filter,
            impl_->object_layer_pair_filter);

        impl_->physics_system.SetGravity(JPH::Vec3(desc.gravity.x, desc.gravity.y, desc.gravity.z));

        impl_->initialized = true;

#if defined(JPH_VERSION_MAJOR) && defined(JPH_VERSION_MINOR) && defined(JPH_VERSION_PATCH)
        const std::string version =
            std::to_string(JPH_VERSION_MAJOR) + "." +
            std::to_string(JPH_VERSION_MINOR) + "." +
            std::to_string(JPH_VERSION_PATCH);
#else
        const std::string version = "5.5.0";
#endif

        RAG_LOG_INFO(
            "Physics system initialized: Jolt ",
            version,
            ", gravity=(",
            desc.gravity.x,
            ",",
            desc.gravity.y,
            ",",
            desc.gravity.z,
            "), layers=",
            static_cast<int>(Layers::NUM_LAYERS));

        return true;
    }

    void PhysicsWorld::Shutdown()
    {
        if (!impl_)
        {
            return;
        }

        if (impl_->initialized)
        {
            JPH::BodyInterface& body_interface = impl_->physics_system.GetBodyInterface();
            for (const JPH::BodyID id : impl_->bodies)
            {
                if (!id.IsInvalid())
                {
                    body_interface.RemoveBody(id);
                    body_interface.DestroyBody(id);
                }
            }
            impl_->bodies.clear();
        }

        impl_->job_system.reset();
        impl_->temp_allocator.reset();

        if (impl_->initialized)
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        impl_.reset();
    }

    bool PhysicsWorld::IsInitialized() const
    {
        return impl_ && impl_->initialized;
    }

    BodyHandle PhysicsWorld::CreateStaticBox(const math::Vec3& position, const math::Vec3& half_extents)
    {
        if (!IsInitialized())
        {
            return {};
        }

        JPH::BoxShapeSettings shape_settings(JPH::Vec3(half_extents.x, half_extents.y, half_extents.z));
        shape_settings.SetEmbedded();
        const JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (shape_result.HasError())
        {
            RAG_LOG_ERROR("Physics: static box shape creation failed: ", shape_result.GetError().c_str());
            return {};
        }

        // shape_result owns the shape reference for the rest of this scope.
        const JPH::Shape* shape = shape_result.Get();
        const JPH::BodyID id = AddShapeBody(
            impl_->physics_system.GetBodyInterface(),
            impl_->bodies,
            shape,
            position,
            JPH::EMotionType::Static,
            Layers::NON_MOVING,
            JPH::EActivation::DontActivate);
        if (id.IsInvalid())
        {
            RAG_LOG_ERROR("Physics: failed to create static box body (body limit reached?).");
            return {};
        }

        return BodyHandle{id.GetIndexAndSequenceNumber()};
    }

    BodyHandle PhysicsWorld::CreateDynamicBox(const math::Vec3& position, const math::Vec3& half_extents)
    {
        if (!IsInitialized())
        {
            return {};
        }

        JPH::BoxShapeSettings shape_settings(JPH::Vec3(half_extents.x, half_extents.y, half_extents.z));
        shape_settings.SetEmbedded();
        const JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (shape_result.HasError())
        {
            RAG_LOG_ERROR("Physics: dynamic box shape creation failed: ", shape_result.GetError().c_str());
            return {};
        }

        // shape_result owns the shape reference for the rest of this scope.
        const JPH::Shape* shape = shape_result.Get();
        const JPH::BodyID id = AddShapeBody(
            impl_->physics_system.GetBodyInterface(),
            impl_->bodies,
            shape,
            position,
            JPH::EMotionType::Dynamic,
            Layers::MOVING,
            JPH::EActivation::Activate);
        if (id.IsInvalid())
        {
            RAG_LOG_ERROR("Physics: failed to create dynamic box body (body limit reached?).");
            return {};
        }

        return BodyHandle{id.GetIndexAndSequenceNumber()};
    }

    BodyHandle PhysicsWorld::CreateDynamicSphere(const math::Vec3& position, f32 radius)
    {
        if (!IsInitialized())
        {
            return {};
        }

        JPH::SphereShapeSettings shape_settings(radius);
        shape_settings.SetEmbedded();
        const JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
        if (shape_result.HasError())
        {
            RAG_LOG_ERROR("Physics: dynamic sphere shape creation failed: ", shape_result.GetError().c_str());
            return {};
        }

        // shape_result owns the shape reference for the rest of this scope.
        const JPH::Shape* shape = shape_result.Get();
        const JPH::BodyID id = AddShapeBody(
            impl_->physics_system.GetBodyInterface(),
            impl_->bodies,
            shape,
            position,
            JPH::EMotionType::Dynamic,
            Layers::MOVING,
            JPH::EActivation::Activate);
        if (id.IsInvalid())
        {
            RAG_LOG_ERROR("Physics: failed to create dynamic sphere body (body limit reached?).");
            return {};
        }

        return BodyHandle{id.GetIndexAndSequenceNumber()};
    }

    void PhysicsWorld::Step(f32 delta_seconds)
    {
        if (!IsInitialized() || delta_seconds <= 0.0f)
        {
            return;
        }

        f32 step_delta = delta_seconds;
        const f32 cap = impl_->desc.max_frame_delta_seconds;
        if (cap > 0.0f && step_delta > cap)
        {
            RAG_LOG_WARNING(
                "Physics: frame delta ",
                delta_seconds,
                "s exceeded the ",
                cap,
                "s cap; clamping to keep the simulation stable.");
            step_delta = cap;
        }

        // Run roughly one collision step per fixed_timestep_seconds so each
        // integration step stays the same size regardless of frame rate.
        int collision_steps = 1;
        const f32 fixed_step = impl_->desc.fixed_timestep_seconds;
        if (fixed_step > 0.0f)
        {
            collision_steps = std::max(1, static_cast<int>(std::ceil(step_delta / fixed_step)));
        }

        const JPH::EPhysicsUpdateError error = impl_->physics_system.Update(
            step_delta,
            collision_steps,
            impl_->temp_allocator.get(),
            impl_->job_system.get());
        if (error != JPH::EPhysicsUpdateError::None)
        {
            RAG_LOG_WARNING("Physics: Jolt update reported error flags ", static_cast<u32>(error), ".");
        }
    }

    BodyState PhysicsWorld::GetBodyState(BodyHandle handle) const
    {
        BodyState state{};
        if (!IsInitialized() || !handle.IsValid())
        {
            return state;
        }

        const JPH::BodyID id(handle.id);
        const JPH::BodyInterface& body_interface = impl_->physics_system.GetBodyInterface();
        const JPH::RVec3 position = body_interface.GetPosition(id);
        const JPH::Quat rotation = body_interface.GetRotation(id);

        state.position = math::Vec3{
            static_cast<f32>(position.GetX()),
            static_cast<f32>(position.GetY()),
            static_cast<f32>(position.GetZ())};
        state.rotation = math::Quat{
            rotation.GetX(),
            rotation.GetY(),
            rotation.GetZ(),
            rotation.GetW()};
        return state;
    }
}
