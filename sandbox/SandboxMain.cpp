#include "Rag/Core/Application.h"
#include "Rag/Core/Event.h"
#include "Rag/Core/Log.h"
#include "Rag/Core/Math.h"
#include "Rag/Scene/Scene.h"

#if defined(RAG_ENABLE_VULKAN)
    #include "Rag/RendererVK/VulkanRenderer.h"
#endif

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

            AnimateScene(frame_time);
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
        }

    private:
        struct SpinningCube
        {
            rag::scene::EntityId entity{};
            rag::f32 yaw_radians_per_second = 0.0f;
            rag::f32 pitch_radians_per_second = 0.0f;
        };

        void BuildScene()
        {
            constexpr rag::u32 RingCubeCount = 8;
            constexpr rag::f32 RingRadius = 4.0f;

            for (rag::u32 index = 0; index < RingCubeCount; ++index)
            {
                const rag::f32 angle = (2.0f * rag::math::Pi * static_cast<rag::f32>(index)) /
                                       static_cast<rag::f32>(RingCubeCount);

                const rag::scene::EntityId entity = scene_.CreateEntity();
                rag::scene::TransformComponent& transform = scene_.AddTransform(entity);
                transform.local_position = rag::math::Vec3{
                    RingRadius * std::cos(angle),
                    0.0f,
                    RingRadius * std::sin(angle)};
                scene_.AddRenderable(entity);

                if ((index % 2) == 0)
                {
                    spinning_cubes_.push_back(SpinningCube{
                        entity,
                        0.6f + (0.15f * static_cast<rag::f32>(index)),
                        0.25f});
                }
            }

            const rag::scene::EntityId center_cube = scene_.CreateEntity();
            rag::scene::TransformComponent& center_transform = scene_.AddTransform(center_cube);
            center_transform.local_scale = rag::math::Vec3{1.5f, 1.5f, 1.5f};
            scene_.AddRenderable(center_cube);
            spinning_cubes_.push_back(SpinningCube{center_cube, 0.9f, 0.5f});

            camera_entity_ = scene_.CreateEntity();
            rag::scene::TransformComponent& camera_transform = scene_.AddTransform(camera_entity_);
            camera_transform.local_position = rag::math::Vec3{0.0f, 3.0f, 9.0f};
            camera_transform.local_rotation_radians = rag::math::Vec3{-0.3f, 0.0f, 0.0f};

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
                " ring cubes, 1 center cube, 1 camera, 1 directional light), ",
                spinning_cubes_.size(),
                " of the cubes spin.");
        }

        void AnimateScene(const rag::core::FrameTime& frame_time)
        {
            const rag::f32 delta_seconds = static_cast<rag::f32>(frame_time.delta_seconds);

            for (const SpinningCube& cube : spinning_cubes_)
            {
                rag::scene::TransformComponent* transform = scene_.GetTransform(cube.entity);
                if (transform == nullptr)
                {
                    continue;
                }

                transform->local_rotation_radians.y += cube.yaw_radians_per_second * delta_seconds;
                transform->local_rotation_radians.x += cube.pitch_radians_per_second * delta_seconds;
                scene_.MarkTransformDirty(cube.entity);
            }
        }

        rag::platform::IWindow* window_ = nullptr;
        rag::scene::Scene scene_;
        rag::renderer::RenderWorld render_world_;
        rag::scene::EntityId camera_entity_{};
        std::vector<SpinningCube> spinning_cubes_;
        rag::f64 time_since_title_update_ = 0.0;
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
