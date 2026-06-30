#include "Rag/Core/Application.h"

#include "Rag/Core/Assert.h"
#include "Rag/Core/Log.h"

#include <chrono>
#include <thread>
#include <utility>

namespace rag::core
{
    Application::Application(ApplicationDesc desc, IApplicationClient& client)
        : desc_(std::move(desc)),
          client_(client)
    {
        RAG_ASSERT(desc_.window_width > 0, "Window width must be greater than zero.");
        RAG_ASSERT(desc_.window_height > 0, "Window height must be greater than zero.");

        platform::WindowDesc window_desc{};
        window_desc.title = desc_.name;
        window_desc.width = desc_.window_width;
        window_desc.height = desc_.window_height;

        window_ = platform::CreatePlatformWindow(window_desc);
        RAG_ASSERT(window_ != nullptr, "Platform window creation failed.");

        window_->SetEventCallback([this](const Event& event) {
            DispatchEvent(event);
        });
    }

    Application::~Application() = default;

    int Application::Run()
    {
        RAG_LOG_INFO("Starting ", desc_.name, " ", RAG_ENGINE_VERSION);

        running_ = true;
        clock_.Reset();
        client_.OnStart();

        if (client_.ShouldClose())
        {
            RequestClose();
        }

        bool window_suspended = false;
        while (running_ && !window_->ShouldClose())
        {
            input_.BeginFrame();
            window_->PollEvents();

            if (!running_ || window_->ShouldClose())
            {
                break;
            }

            const bool should_suspend =
                window_->IsWindowMinimized() ||
                window_->Width() == 0 ||
                window_->Height() == 0;
            if (should_suspend)
            {
                if (!window_suspended)
                {
                    RAG_LOG_INFO("Window rendering suspended while minimized or zero-sized.");
                    window_suspended = true;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                clock_.ResyncDelta();
                continue;
            }

            if (window_suspended)
            {
                RAG_LOG_INFO("Window rendering resumed.");
                window_suspended = false;
            }

            const FrameTime frame_time = clock_.Tick();
            client_.OnUpdate(frame_time, input_);

            if (client_.ShouldClose())
            {
                RequestClose();
            }
        }

        client_.OnStop();
        RAG_LOG_INFO("Shutting down ", desc_.name);
        return 0;
    }

    void Application::RequestClose()
    {
        running_ = false;
    }

    const ApplicationDesc& Application::Description() const
    {
        return desc_;
    }

    const InputState& Application::Input() const
    {
        return input_;
    }

    platform::IWindow& Application::Window()
    {
        return *window_;
    }

    void Application::DispatchEvent(const Event& event)
    {
        input_.ApplyEvent(event);

        if (event.type == EventType::WindowClose)
        {
            RequestClose();
        }

        client_.OnEvent(event);
    }
}
