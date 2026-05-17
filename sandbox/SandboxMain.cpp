#include "Rag/Core/Application.h"
#include "Rag/Core/Event.h"
#include "Rag/Core/Log.h"
#include "Rag/Scene/Scene.h"

#if defined(RAG_ENABLE_VULKAN)
    #include "Rag/RendererVK/VulkanRenderer.h"
#endif

#include <exception>
#include <string_view>
#include <variant>

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
            BuildDemoScene();
            RAG_LOG_INFO("Sandbox started. Press Escape to quit.");
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

            scene_.Update();
            scene_.ExtractRenderWorld(render_world_);

#if defined(RAG_ENABLE_VULKAN)
            if (renderer_ != nullptr)
            {
                rag::renderer::RenderFrameContext render_context{};
                render_context.clear_color = {0.015f, 0.018f, 0.026f, 1.0f};
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
                    "s mouse=(",
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
        void BuildDemoScene()
        {
            if (scene_initialized_)
            {
                return;
            }

            const rag::scene::EntityId camera_entity = scene_.CreateEntity();
            rag::scene::TransformComponent& camera_transform = scene_.AddTransform(camera_entity);
            camera_transform.local_position = rag::math::Vec3{0.0f, 0.0f, 5.0f};

            rag::scene::CameraComponent& camera = scene_.AddCamera(camera_entity);
            camera.active = true;
            camera.aspect_ratio = 16.0f / 9.0f;
            scene_.SetActiveCamera(camera_entity);

            const rag::scene::EntityId object_entity = scene_.CreateEntity();
            rag::scene::TransformComponent& object_transform = scene_.AddTransform(object_entity);
            object_transform.local_position = rag::math::Vec3{0.0f, 0.0f, 0.0f};

            rag::scene::RenderableComponent& renderable = scene_.AddRenderable(object_entity);
            renderable.local_bounds.center = rag::math::Vec3{};
            renderable.local_bounds.extents = rag::math::Vec3{0.5f, 0.5f, 0.5f};

            const rag::scene::EntityId light_entity = scene_.CreateEntity();
            rag::scene::TransformComponent& light_transform = scene_.AddTransform(light_entity);
            light_transform.local_rotation_radians = rag::math::Vec3{-0.7f, 0.35f, 0.0f};

            rag::scene::LightComponent& light = scene_.AddLight(light_entity);
            light.type = rag::renderer::RenderLightType::Directional;
            light.intensity = 2.0f;

            scene_.Update();
            scene_.ExtractRenderWorld(render_world_);
            RAG_LOG_INFO(
                "Scene initialized with ",
                scene_.EntityCount(),
                " entities, ",
                render_world_.objects.size(),
                " render objects, and ",
                render_world_.lights.size(),
                " lights.");

            scene_initialized_ = true;
        }

        rag::scene::Scene scene_;
        rag::renderer::RenderWorld render_world_;
        rag::f64 time_since_title_update_ = 0.0;
        bool scene_initialized_ = false;
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
