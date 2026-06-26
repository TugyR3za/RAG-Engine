#include "Rag/Core/Application.h"
#include "Rag/Core/Event.h"
#include "Rag/Core/Log.h"
#include "Rag/Core/Math.h"
#include "Rag/Scene/Scene.h"

#if defined(RAG_ENABLE_PHYSICS)
    #include "Rag/Physics/PhysicsWorld.h"
#endif

#if defined(RAG_ENABLE_VULKAN)
    #include "Rag/RendererVK/VulkanRenderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <exception>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    class SandboxClient final : public rag::core::IApplicationClient
    {
    public:
        explicit SandboxClient(bool smoke_test)
            : smoke_test_(smoke_test)
        {
        }

        void OnStart() override
        {
            RAG_LOG_INFO("RAG Engine Vulkan scene sandbox started. Press Escape to quit, F11 to toggle fullscreen.");
            RAG_LOG_INFO(
                "Free-fly camera: WASD move, Space/E up, Left Ctrl/Q down, "
                "hold Right Mouse to look, Left Shift to sprint.");
            InitializePhysics();
            BuildScene();

            if (smoke_test_)
            {
#if defined(RAG_ENABLE_VULKAN)
                RAG_LOG_INFO("Smoke test requested; rendering one Vulkan frame before shutdown.");
#else
                RAG_LOG_INFO("Smoke test requested; closing after startup.");
                requested_close_ = true;
#endif
            }
        }

        void InitializeRenderer(rag::platform::IWindow& window)
        {
            window_ = &window;

#if defined(RAG_ENABLE_VULKAN)
            rag::renderer::RendererDesc renderer_desc{};
            renderer_desc.backend = rag::renderer::RendererBackend::Vulkan;
            renderer_desc.window = &window;
            renderer_desc.application_name = "RAG Engine Sandbox";
            renderer_desc.frames_in_flight = 2;

#if defined(RAG_DEBUG)
            renderer_desc.enable_validation = true;
#else
            renderer_desc.enable_validation = false;
#endif

            renderer_ = rag::renderer::vk::CreateVulkanRenderer(renderer_desc);
#else
            (void)window;
            RAG_LOG_WARNING("Vulkan renderer is not compiled; sandbox is running the Phase 1 window loop only.");
#endif
        }

        void OnEvent(const rag::core::Event& event) override
        {
            if (event.type == rag::core::EventType::WindowResize)
            {
                const auto& resize = std::get<rag::core::WindowResizeEvent>(event.payload);
                RAG_LOG_INFO("Window resized to ", resize.width, "x", resize.height);

                if (resize.width > 0 && resize.height > 0)
                {
                    if (rag::scene::CameraComponent* camera = scene_.GetCamera(camera_entity_))
                    {
                        camera->aspect_ratio =
                            static_cast<rag::f32>(resize.width) /
                            static_cast<rag::f32>(resize.height);
                    }
                }

#if defined(RAG_ENABLE_VULKAN)
                if (renderer_ != nullptr)
                {
                    renderer_->OnWindowResized(resize.width, resize.height);
                }
#endif
            }
        }

        void OnUpdate(
            const rag::core::FrameTime& frame_time,
            const rag::core::InputState& input) override
        {
            if (input.WasKeyPressed(rag::core::KeyCode::Escape))
            {
                requested_close_ = true;
            }

            if (input.WasKeyPressed(rag::core::KeyCode::F11) && window_ != nullptr)
            {
                window_->SetFullscreen(!window_->IsFullscreen());
            }

            UpdateCamera(frame_time, input);
            StepPhysics(frame_time);
            scene_.Update();
            scene_.ExtractRenderWorld(render_world_);

#if defined(RAG_ENABLE_VULKAN)
            if (renderer_ != nullptr)
            {
                rag::renderer::RenderFrameContext render_context{};
                render_context.clear_color = {0.02f, 0.02f, 0.06f, 1.0f};
                render_context.render_world = &render_world_;
                renderer_->RenderFrame(render_context);
            }
#endif

            if (smoke_test_)
            {
                requested_close_ = true;
            }

            time_since_title_update_ += frame_time.delta_seconds;
            if (time_since_title_update_ >= 1.0)
            {
                RAG_LOG_TRACE(
                    "Frame ",
                    frame_time.frame_index,
                    " dt=",
                    frame_time.delta_seconds,
                    "s objects=",
                    render_world_.objects.size(),
                    " mouse=(",
                    input.MouseX(),
                    ", ",
                    input.MouseY(),
                    ")");
                time_since_title_update_ = 0.0;
            }
        }

        [[nodiscard]] bool ShouldClose() const override
        {
            return requested_close_;
        }

        void OnStop() override
        {
#if defined(RAG_ENABLE_VULKAN)
            if (renderer_ != nullptr)
            {
                renderer_->WaitIdle();
                renderer_.reset();
            }
#endif

#if defined(RAG_ENABLE_PHYSICS)
            physics_.Shutdown();
#endif
        }

    private:
#if defined(RAG_ENABLE_PHYSICS)
        // Links a dynamic Jolt body to the scene entity it drives.
        struct PhysicsBody
        {
            rag::scene::EntityId entity{};
            rag::physics::BodyHandle body{};
        };

        void InitializePhysics()
        {
            rag::physics::PhysicsWorldDesc desc{};
            desc.gravity = rag::math::Vec3{0.0f, -9.81f, 0.0f};
            desc.fixed_timestep_seconds = 1.0f / 60.0f;
            desc.max_frame_delta_seconds = 0.033f;

            physics_ready_ = physics_.Initialize(desc);
            if (!physics_ready_)
            {
                RAG_LOG_ERROR("Physics initialization failed; scene objects will not be simulated.");
            }
        }
#else
        void InitializePhysics() {}
#endif

        void BuildScene()
        {
            constexpr rag::u32 RingCubeCount = 8;
            constexpr rag::f32 RingRadius = 4.0f;
            constexpr rag::f32 RingCubeScale = 1.0f;
            constexpr rag::f32 CenterCubeScale = 1.5f;

            // A unit cube spans +/-0.5, so the physics half-extent is half the
            // visual scale on each axis.
            constexpr rag::f32 RingCubeHalfExtent = 0.5f * RingCubeScale;
            constexpr rag::f32 CenterCubeHalfExtent = 0.5f * CenterCubeScale;
            constexpr rag::f32 SphereRadius = 1.0f; // models/sphere.obj is a unit sphere.

            // Dynamic bodies start elevated so they visibly fall and settle on
            // the floor at startup. Tweak these to change the drop heights.
            constexpr rag::f32 RingCubeStartHeight = 7.0f;
            constexpr rag::f32 RingCubeStartStagger = 0.6f; // raises each successive cube
            constexpr rag::f32 CenterCubeStartHeight = 12.0f;
            constexpr rag::f32 SphereStartHeight = 9.0f;

            // The static ground collider matches the visual ground plane: a unit
            // cube mesh scaled to (40, 0.2, 40) centred at y=-0.6, giving a top
            // surface at y=-0.5.
            constexpr rag::math::Vec3 GroundCenter{0.0f, -0.6f, 0.0f};
            constexpr rag::math::Vec3 GroundScale{40.0f, 0.2f, 40.0f};
            constexpr rag::math::Vec3 GroundHalfExtents{20.0f, 0.1f, 20.0f};

            // Duck.glb's root node applies a 0.01 scale. Its geometry starts
            // roughly 0.1 units above local Y=0, so this rests it on the floor.
            // The duck is left static (not simulated) to keep things simple.
            constexpr rag::f32 DuckGroundOffset = -0.6f;

            // Renderer mesh registry convention: slot 0 = built-in cube,
            // slot 1 = OBJ sphere, slot 2 = tiled-UV ground, slot 3 = glTF duck.
            constexpr rag::renderer::MeshHandle CubeMeshHandle{0, 0};
            constexpr rag::renderer::MeshHandle SphereMeshHandle{1, 0};
            constexpr rag::renderer::MeshHandle GroundMeshHandle{2, 0};
            constexpr rag::renderer::MeshHandle DuckMeshHandle{3, 0};

            for (rag::u32 index = 0; index < RingCubeCount; ++index)
            {
                const rag::f32 angle = (2.0f * rag::math::Pi * static_cast<rag::f32>(index)) /
                                       static_cast<rag::f32>(RingCubeCount);

                const rag::math::Vec3 start_position{
                    RingRadius * std::cos(angle),
                    RingCubeStartHeight + (RingCubeStartStagger * static_cast<rag::f32>(index)),
                    RingRadius * std::sin(angle)};

                const rag::scene::EntityId entity = scene_.CreateEntity();
                scene_.AddTransform(entity).local_position = start_position;
                scene_.AddRenderable(entity).mesh = CubeMeshHandle;
                SpawnDynamicBox(
                    entity,
                    start_position,
                    rag::math::Vec3{RingCubeHalfExtent, RingCubeHalfExtent, RingCubeHalfExtent});
            }

            const rag::math::Vec3 center_start{0.0f, CenterCubeStartHeight, 0.0f};
            const rag::scene::EntityId center_cube = scene_.CreateEntity();
            rag::scene::TransformComponent& center_transform = scene_.AddTransform(center_cube);
            center_transform.local_position = center_start;
            center_transform.local_scale =
                rag::math::Vec3{CenterCubeScale, CenterCubeScale, CenterCubeScale};
            scene_.AddRenderable(center_cube).mesh = CubeMeshHandle;
            SpawnDynamicBox(
                center_cube,
                center_start,
                rag::math::Vec3{CenterCubeHalfExtent, CenterCubeHalfExtent, CenterCubeHalfExtent});

            const rag::scene::EntityId ground = scene_.CreateEntity();
            rag::scene::TransformComponent& ground_transform = scene_.AddTransform(ground);
            ground_transform.local_position = GroundCenter;
            ground_transform.local_scale = GroundScale;
            scene_.AddRenderable(ground).mesh = GroundMeshHandle;
            SpawnStaticBox(GroundCenter, GroundHalfExtents);

            // The OBJ sphere drops onto open floor off to the side of the ring so
            // its shadow stays clearly visible as it falls and lands.
            const rag::math::Vec3 sphere_start{6.5f, SphereStartHeight, 0.0f};
            const rag::scene::EntityId sphere = scene_.CreateEntity();
            scene_.AddTransform(sphere).local_position = sphere_start;
            rag::scene::RenderableComponent& sphere_renderable = scene_.AddRenderable(sphere);
            sphere_renderable.mesh = SphereMeshHandle;
            sphere_renderable.local_bounds.extents =
                rag::math::Vec3{SphereRadius, SphereRadius, SphereRadius};
            SpawnDynamicSphere(sphere, sphere_start, SphereRadius);

            const rag::scene::EntityId duck = scene_.CreateEntity();
            rag::scene::TransformComponent& duck_transform = scene_.AddTransform(duck);
            duck_transform.local_position =
                rag::math::Vec3{-6.5f, DuckGroundOffset, 0.0f};
            rag::scene::RenderableComponent& duck_renderable = scene_.AddRenderable(duck);
            duck_renderable.mesh = DuckMeshHandle;
            duck_renderable.local_bounds.extents = rag::math::Vec3{1.0f, 1.0f, 1.0f};

            camera_entity_ = scene_.CreateEntity();
            rag::scene::TransformComponent& camera_transform = scene_.AddTransform(camera_entity_);
            camera_transform.local_position = rag::math::Vec3{0.0f, 3.0f, 9.0f};
            camera_yaw_radians_ = 0.0f;
            camera_pitch_radians_ = -0.3f;
            camera_transform.local_rotation_radians =
                rag::math::Vec3{camera_pitch_radians_, camera_yaw_radians_, 0.0f};

            rag::scene::CameraComponent& camera = scene_.AddCamera(camera_entity_);
            camera.vertical_fov_radians = rag::math::Pi / 3.0f;
            camera.near_plane = 0.1f;
            camera.far_plane = 100.0f;
            if (window_ != nullptr && window_->Width() > 0 && window_->Height() > 0)
            {
                camera.aspect_ratio =
                    static_cast<rag::f32>(window_->Width()) /
                    static_cast<rag::f32>(window_->Height());
            }
            scene_.SetActiveCamera(camera_entity_);

            const rag::scene::EntityId light_entity = scene_.CreateEntity();
            rag::scene::TransformComponent& light_transform = scene_.AddTransform(light_entity);
            light_transform.local_rotation_radians = rag::math::Vec3{0.65f, 0.0f, 0.65f};

            rag::scene::LightComponent& light = scene_.AddLight(light_entity);
            light.type = rag::renderer::RenderLightType::Directional;
            light.color = rag::math::Vec3{1.0f, 0.92f, 0.78f};
            light.intensity = 1.25f;

            RAG_LOG_INFO(
                "Built sandbox scene: ",
                scene_.EntityCount(),
                " entities (",
                RingCubeCount,
                " ring cubes, 1 center cube, 1 obj sphere, 1 glTF duck, "
                "1 ground, 1 camera, 1 directional light), ",
                DynamicBodyCount(),
                " dynamic bodies falling under gravity.");
        }

#if defined(RAG_ENABLE_PHYSICS)
        void SpawnDynamicBox(
            rag::scene::EntityId entity,
            const rag::math::Vec3& position,
            const rag::math::Vec3& half_extents)
        {
            if (!physics_ready_)
            {
                return;
            }

            const rag::physics::BodyHandle body = physics_.CreateDynamicBox(position, half_extents);
            if (!body.IsValid())
            {
                return;
            }

            physics_bodies_.push_back(PhysicsBody{entity, body});
            if (rag::scene::TransformComponent* transform = scene_.GetTransform(entity))
            {
                transform->use_quaternion_rotation = true;
            }
        }

        void SpawnDynamicSphere(
            rag::scene::EntityId entity,
            const rag::math::Vec3& position,
            rag::f32 radius)
        {
            if (!physics_ready_)
            {
                return;
            }

            const rag::physics::BodyHandle body = physics_.CreateDynamicSphere(position, radius);
            if (!body.IsValid())
            {
                return;
            }

            physics_bodies_.push_back(PhysicsBody{entity, body});
            if (rag::scene::TransformComponent* transform = scene_.GetTransform(entity))
            {
                transform->use_quaternion_rotation = true;
            }
        }

        void SpawnStaticBox(const rag::math::Vec3& position, const rag::math::Vec3& half_extents)
        {
            if (physics_ready_)
            {
                (void)physics_.CreateStaticBox(position, half_extents);
            }
        }

        [[nodiscard]] std::size_t DynamicBodyCount() const
        {
            return physics_bodies_.size();
        }

        void StepPhysics(const rag::core::FrameTime& frame_time)
        {
            if (!physics_ready_)
            {
                return;
            }

            physics_.Step(static_cast<rag::f32>(frame_time.delta_seconds));

            // Write each simulated body's pose back onto its scene transform so
            // the renderer draws every object where physics put it.
            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                rag::scene::TransformComponent* transform = scene_.GetTransform(physics_body.entity);
                if (transform == nullptr)
                {
                    continue;
                }

                const rag::physics::BodyState state = physics_.GetBodyState(physics_body.body);
                transform->local_position = state.position;
                transform->local_rotation_quat = state.rotation;
                transform->use_quaternion_rotation = true;
                scene_.MarkTransformDirty(physics_body.entity);
            }
        }
#else
        void SpawnDynamicBox(rag::scene::EntityId, const rag::math::Vec3&, const rag::math::Vec3&) {}
        void SpawnDynamicSphere(rag::scene::EntityId, const rag::math::Vec3&, rag::f32) {}
        void SpawnStaticBox(const rag::math::Vec3&, const rag::math::Vec3&) {}
        [[nodiscard]] std::size_t DynamicBodyCount() const { return 0; }
        void StepPhysics(const rag::core::FrameTime&) {}
#endif

        void UpdateCamera(
            const rag::core::FrameTime& frame_time,
            const rag::core::InputState& input)
        {
            rag::scene::TransformComponent* transform = scene_.GetTransform(camera_entity_);
            if (transform == nullptr)
            {
                return;
            }

            bool transform_changed = false;
            const rag::i32 mouse_x = input.MouseX();
            const rag::i32 mouse_y = input.MouseY();
            const bool mouse_look = input.IsMouseButtonDown(rag::core::MouseButton::Right);

            if (mouse_look)
            {
                if (mouse_look_active_)
                {
                    const rag::i32 delta_x = mouse_x - previous_mouse_x_;
                    const rag::i32 delta_y = mouse_y - previous_mouse_y_;

                    camera_yaw_radians_ -= static_cast<rag::f32>(delta_x) * MouseSensitivity;
                    camera_pitch_radians_ -= static_cast<rag::f32>(delta_y) * MouseSensitivity;
                    camera_pitch_radians_ = std::clamp(
                        camera_pitch_radians_,
                        -MaximumPitchRadians,
                        MaximumPitchRadians);
                    transform_changed = delta_x != 0 || delta_y != 0;
                }

                mouse_look_active_ = true;
            }
            else
            {
                mouse_look_active_ = false;
            }

            previous_mouse_x_ = mouse_x;
            previous_mouse_y_ = mouse_y;

            const rag::f32 cos_pitch = std::cos(camera_pitch_radians_);
            const rag::math::Vec3 forward = rag::math::Normalize(rag::math::Vec3{
                -std::sin(camera_yaw_radians_) * cos_pitch,
                std::sin(camera_pitch_radians_),
                -std::cos(camera_yaw_radians_) * cos_pitch});
            const rag::math::Vec3 right = rag::math::Normalize(rag::math::Vec3{
                std::cos(camera_yaw_radians_),
                0.0f,
                -std::sin(camera_yaw_radians_)});
            constexpr rag::math::Vec3 WorldUp{0.0f, 1.0f, 0.0f};

            rag::math::Vec3 movement{};
            if (input.IsKeyDown(rag::core::KeyCode::W))
            {
                movement = movement + forward;
            }
            if (input.IsKeyDown(rag::core::KeyCode::S))
            {
                movement = movement - forward;
            }
            if (input.IsKeyDown(rag::core::KeyCode::D))
            {
                movement = movement + right;
            }
            if (input.IsKeyDown(rag::core::KeyCode::A))
            {
                movement = movement - right;
            }
            if (input.IsKeyDown(rag::core::KeyCode::Space) ||
                input.IsKeyDown(rag::core::KeyCode::E))
            {
                movement = movement + WorldUp;
            }
            if (input.IsKeyDown(rag::core::KeyCode::LeftControl) ||
                input.IsKeyDown(rag::core::KeyCode::Q))
            {
                movement = movement - WorldUp;
            }

            if (rag::math::Length(movement) > 0.000001f)
            {
                const rag::f32 speed =
                    input.IsKeyDown(rag::core::KeyCode::LeftShift)
                        ? MoveSpeed * SprintMultiplier
                        : MoveSpeed;
                const rag::f32 distance =
                    speed * static_cast<rag::f32>(frame_time.delta_seconds);
                transform->local_position =
                    transform->local_position + (rag::math::Normalize(movement) * distance);
                transform_changed = true;
            }

            if (transform_changed)
            {
                transform->local_rotation_radians =
                    rag::math::Vec3{camera_pitch_radians_, camera_yaw_radians_, 0.0f};
                scene_.MarkTransformDirty(camera_entity_);
            }
        }

        rag::platform::IWindow* window_ = nullptr;
        rag::scene::Scene scene_;
        rag::renderer::RenderWorld render_world_;
        rag::scene::EntityId camera_entity_{};

#if defined(RAG_ENABLE_PHYSICS)
        rag::physics::PhysicsWorld physics_;
        std::vector<PhysicsBody> physics_bodies_;
        bool physics_ready_ = false;
#endif

        static constexpr rag::f32 MoveSpeed = 4.5f;
        static constexpr rag::f32 SprintMultiplier = 4.0f;
        static constexpr rag::f32 MouseSensitivity = 0.0025f;
        static constexpr rag::f32 MaximumPitchRadians = (rag::math::Pi * 0.5f) - 0.01f;
        rag::f32 camera_yaw_radians_ = 0.0f;
        rag::f32 camera_pitch_radians_ = 0.0f;
        rag::f64 time_since_title_update_ = 0.0;
        rag::i32 previous_mouse_x_ = 0;
        rag::i32 previous_mouse_y_ = 0;
        bool mouse_look_active_ = false;
        bool smoke_test_ = false;
        bool requested_close_ = false;

#if defined(RAG_ENABLE_VULKAN)
        rag::renderer::RendererPtr renderer_;
#endif
    };
}

int main(int argc, char** argv)
{
    try
    {
        const bool smoke_test = argc > 1 && std::string_view(argv[1]) == "--smoke-test";
        SandboxClient client(smoke_test);

        rag::core::ApplicationDesc desc{};
        desc.name = "RAG Engine Sandbox";
        desc.window_width = 1280;
        desc.window_height = 720;

        rag::core::Application app(desc, client);
        client.InitializeRenderer(app.Window());
        return app.Run();
    }
    catch (const std::exception& exception)
    {
        RAG_LOG_FATAL("Fatal error: ", exception.what());
        return 1;
    }
}
