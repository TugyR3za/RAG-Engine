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
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
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

        // Builds the unscaled base collider for a descriptor: a box or sphere,
        // optionally wrapped in a RotatedTranslatedShape so the collider sits at
        // BodyDesc::offset from the entity origin (for off-centre-pivot meshes).
        // The transform scale is applied later so this base stays reusable.
        JPH::ShapeSettings::ShapeResult BuildBaseShape(const BodyDesc& desc)
        {
            JPH::ShapeSettings::ShapeResult inner;
            if (desc.shape == ColliderShape::Sphere)
            {
                JPH::SphereShapeSettings sphere(desc.sphere_radius);
                sphere.SetEmbedded();
                inner = sphere.Create();
            }
            else
            {
                JPH::BoxShapeSettings box(JPH::Vec3(
                    desc.box_half_extents.x,
                    desc.box_half_extents.y,
                    desc.box_half_extents.z));
                box.SetEmbedded();
                inner = box.Create();
            }

            if (inner.HasError())
            {
                return inner;
            }

            const bool has_offset =
                desc.offset.x != 0.0f || desc.offset.y != 0.0f || desc.offset.z != 0.0f;
            if (!has_offset)
            {
                return inner;
            }

            JPH::RotatedTranslatedShapeSettings offset_shape(
                JPH::Vec3(desc.offset.x, desc.offset.y, desc.offset.z),
                JPH::Quat::sIdentity(),
                inner.Get());
            offset_shape.SetEmbedded();
            return offset_shape.Create();
        }

        JPH::EMotionType ToJoltMotionType(BodyMode mode)
        {
            return mode == BodyMode::Static ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
        }

        constexpr f32 MinimumColliderScale = 0.01f;
        constexpr f32 ColliderScaleEpsilon = 0.0001f;

        f32 SanitizeScaleAxis(f32 scale)
        {
            if (!std::isfinite(scale))
            {
                return 1.0f;
            }
            return std::max(std::abs(scale), MinimumColliderScale);
        }

        math::Vec3 SanitizeScale(const math::Vec3& scale)
        {
            return math::Vec3{
                SanitizeScaleAxis(scale.x),
                SanitizeScaleAxis(scale.y),
                SanitizeScaleAxis(scale.z)};
        }

        bool ScalesMatch(const math::Vec3& left, const math::Vec3& right)
        {
            return
                std::abs(left.x - right.x) <= ColliderScaleEpsilon &&
                std::abs(left.y - right.y) <= ColliderScaleEpsilon &&
                std::abs(left.z - right.z) <= ColliderScaleEpsilon;
        }

        // The scale actually applied to a collider: sanitized against zero/NaN,
        // and collapsed to the largest axis for spheres (Jolt only allows uniform
        // sphere scale, so the collider conservatively encloses the visual).
        math::Vec3 EffectiveScale(ColliderShape shape, const math::Vec3& raw_scale)
        {
            math::Vec3 scale = SanitizeScale(raw_scale);
            if (shape == ColliderShape::Sphere)
            {
                const f32 uniform = std::max(scale.x, std::max(scale.y, scale.z));
                scale = math::Vec3{uniform, uniform, uniform};
            }
            return scale;
        }
    }

    struct PhysicsWorld::Impl
    {
        // One record per live body (static and dynamic). base_shape is the
        // immutable unscaled collider; every scale replacement derives from it,
        // never from the current (already-scaled) shape, so repeated edits cannot
        // drift. motion_type gates velocity/activation handling so operations are
        // safe on static bodies.
        struct BodyRecord
        {
            JPH::BodyID id;
            JPH::RefConst<JPH::Shape> base_shape;
            ColliderShape shape_type = ColliderShape::Box;
            JPH::EMotionType motion_type = JPH::EMotionType::Dynamic;
            math::Vec3 applied_scale{1.0f, 1.0f, 1.0f};
        };

        PhysicsWorldDesc desc{};
        bool initialized = false;

        std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
        std::unique_ptr<JPH::JobSystemThreadPool> job_system;
        BroadPhaseLayerImpl broad_phase_layer_interface;
        ObjectVsBroadPhaseFilterImpl object_vs_broad_phase_filter;
        ObjectLayerPairFilterImpl object_layer_pair_filter;
        JPH::PhysicsSystem physics_system;
        std::vector<BodyRecord> bodies;

        BodyRecord* FindRecord(const JPH::BodyID& id)
        {
            for (BodyRecord& record : bodies)
            {
                if (record.id == id)
                {
                    return &record;
                }
            }
            return nullptr;
        }
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
            for (const Impl::BodyRecord& record : impl_->bodies)
            {
                if (!record.id.IsInvalid())
                {
                    body_interface.RemoveBody(record.id);
                    body_interface.DestroyBody(record.id);
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

    BodyHandle PhysicsWorld::CreateBody(const BodyDesc& desc)
    {
        if (!IsInitialized() || desc.mode == BodyMode::None)
        {
            return {};
        }

        const JPH::ShapeSettings::ShapeResult base_result = BuildBaseShape(desc);
        if (base_result.HasError())
        {
            RAG_LOG_ERROR("Physics: collider shape creation failed: ", base_result.GetError().c_str());
            return {};
        }

        // base_shape is the immutable unscaled collider kept for later rescales;
        // the transform scale is baked into the shape handed to the body so it
        // spawns correctly sized without waiting for a scale-sync pass.
        const JPH::RefConst<JPH::Shape> base_shape = base_result.Get();
        const math::Vec3 applied_scale = EffectiveScale(desc.shape, desc.scale);

        JPH::RefConst<JPH::Shape> body_shape = base_shape;
        if (!ScalesMatch(applied_scale, math::Vec3{1.0f, 1.0f, 1.0f}))
        {
            const JPH::Shape::ShapeResult scaled = base_shape->ScaleShape(
                JPH::Vec3(applied_scale.x, applied_scale.y, applied_scale.z));
            if (scaled.HasError())
            {
                RAG_LOG_ERROR("Physics: initial collider scale failed: ", scaled.GetError().c_str());
                return {};
            }
            body_shape = scaled.Get();
        }

        const JPH::EMotionType motion_type = ToJoltMotionType(desc.mode);
        const bool is_dynamic = motion_type == JPH::EMotionType::Dynamic;

        JPH::BodyCreationSettings settings(
            body_shape.GetPtr(),
            JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
            JPH::Quat(desc.rotation.x, desc.rotation.y, desc.rotation.z, desc.rotation.w),
            motion_type,
            is_dynamic ? Layers::MOVING : Layers::NON_MOVING);

        if (is_dynamic)
        {
            // CCD (linear cast) stops a fast prop from tunnelling through a thin
            // or resting body; gravity factor 0 keeps a dynamic body in place
            // while still colliding and reacting to pushes.
            settings.mMotionQuality = desc.continuous_collision
                ? JPH::EMotionQuality::LinearCast
                : JPH::EMotionQuality::Discrete;
            settings.mGravityFactor = desc.gravity_enabled ? 1.0f : 0.0f;
        }

        JPH::BodyInterface& body_interface = impl_->physics_system.GetBodyInterface();
        const JPH::BodyID id = body_interface.CreateAndAddBody(
            settings,
            is_dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (id.IsInvalid())
        {
            RAG_LOG_ERROR("Physics: failed to create body (body limit reached?).");
            return {};
        }

        impl_->bodies.push_back(Impl::BodyRecord{
            id,
            base_shape,
            desc.shape,
            motion_type,
            applied_scale});

        return BodyHandle{id.GetIndexAndSequenceNumber()};
    }

    void PhysicsWorld::DestroyBody(BodyHandle handle)
    {
        if (!IsInitialized() || !handle.IsValid())
        {
            return;
        }

        const JPH::BodyID id(handle.id);
        for (auto it = impl_->bodies.begin(); it != impl_->bodies.end(); ++it)
        {
            if (it->id == id)
            {
                JPH::BodyInterface& body_interface = impl_->physics_system.GetBodyInterface();
                body_interface.RemoveBody(id);
                body_interface.DestroyBody(id);
                impl_->bodies.erase(it);
                return;
            }
        }
    }

    BodyHandle PhysicsWorld::CreateStaticBox(const math::Vec3& position, const math::Vec3& half_extents)
    {
        BodyDesc desc;
        desc.shape = ColliderShape::Box;
        desc.box_half_extents = half_extents;
        desc.position = position;
        desc.mode = BodyMode::Static;
        return CreateBody(desc);
    }

    BodyHandle PhysicsWorld::CreateDynamicBox(const math::Vec3& position, const math::Vec3& half_extents)
    {
        BodyDesc desc;
        desc.shape = ColliderShape::Box;
        desc.box_half_extents = half_extents;
        desc.position = position;
        desc.mode = BodyMode::Dynamic;
        return CreateBody(desc);
    }

    BodyHandle PhysicsWorld::CreateDynamicSphere(const math::Vec3& position, f32 radius)
    {
        BodyDesc desc;
        desc.shape = ColliderShape::Sphere;
        desc.sphere_radius = radius;
        desc.position = position;
        desc.mode = BodyMode::Dynamic;
        return CreateBody(desc);
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

    bool PhysicsWorld::SetBodyScale(BodyHandle handle, const math::Vec3& scale)
    {
        if (!IsInitialized() || !handle.IsValid())
        {
            return false;
        }

        const JPH::BodyID id(handle.id);
        Impl::BodyRecord* record = impl_->FindRecord(id);
        if (record == nullptr)
        {
            return false;
        }

        const math::Vec3 applied_scale = EffectiveScale(record->shape_type, scale);
        if (ScalesMatch(record->applied_scale, applied_scale))
        {
            return true;
        }

        const JPH::Shape::ShapeResult scaled_shape = record->base_shape->ScaleShape(
            JPH::Vec3(applied_scale.x, applied_scale.y, applied_scale.z));
        if (scaled_shape.HasError())
        {
            RAG_LOG_ERROR(
                "Physics: failed to scale collider: ",
                scaled_shape.GetError().c_str());
            return false;
        }

        const bool is_dynamic = record->motion_type == JPH::EMotionType::Dynamic;
        JPH::BodyInterface& body_interface = impl_->physics_system.GetBodyInterface();
        body_interface.SetShape(
            id,
            scaled_shape.Get(),
            is_dynamic, // only dynamic bodies have mass properties to recompute
            is_dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (is_dynamic)
        {
            // Clear velocity so growing the collider into a neighbour does not
            // fling the body; static bodies have no velocity to clear.
            body_interface.SetLinearAndAngularVelocity(
                id,
                JPH::Vec3::sZero(),
                JPH::Vec3::sZero());
            body_interface.ActivateBody(id);
        }
        record->applied_scale = applied_scale;
        return true;
    }

    void PhysicsWorld::SetBodyGravityEnabled(BodyHandle handle, bool enabled)
    {
        if (!IsInitialized() || !handle.IsValid())
        {
            return;
        }

        const JPH::BodyID id(handle.id);
        const Impl::BodyRecord* record = impl_->FindRecord(id);
        if (record == nullptr || record->motion_type != JPH::EMotionType::Dynamic)
        {
            return;
        }

        JPH::BodyInterface& body_interface = impl_->physics_system.GetBodyInterface();
        body_interface.SetGravityFactor(id, enabled ? 1.0f : 0.0f);
        body_interface.ActivateBody(id);
    }

    void PhysicsWorld::SetGravity(const math::Vec3& gravity)
    {
        if (!IsInitialized())
        {
            return;
        }

        impl_->desc.gravity = gravity;
        impl_->physics_system.SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }

    void PhysicsWorld::SetBodyPose(
        BodyHandle handle,
        const math::Vec3& position,
        const math::Quat& rotation)
    {
        if (!IsInitialized() || !handle.IsValid())
        {
            return;
        }

        const JPH::BodyID id(handle.id);
        const Impl::BodyRecord* record = impl_->FindRecord(id);
        if (record == nullptr)
        {
            return;
        }

        const bool is_dynamic = record->motion_type == JPH::EMotionType::Dynamic;
        JPH::BodyInterface& body_interface = impl_->physics_system.GetBodyInterface();
        body_interface.SetPositionAndRotation(
            id,
            JPH::RVec3(position.x, position.y, position.z),
            JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
            is_dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (is_dynamic)
        {
            body_interface.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
            body_interface.ActivateBody(id);
        }
    }
}
