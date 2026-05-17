#pragma once

#include "Rag/Core/Clock.h"
#include "Rag/Core/Event.h"
#include "Rag/Core/Input.h"
#include "Rag/Platform/Window.h"

#include <memory>
#include <string>
#include <vector>

namespace rag::core
{
    struct ApplicationDesc
    {
        std::string name = "RAG Engine";
        u32 window_width = 1280;
        u32 window_height = 720;
    };

    class IApplicationClient
    {
    public:
        virtual ~IApplicationClient() = default;

        virtual void OnStart() {}
        virtual void OnEvent(const Event& event) { (void)event; }
        virtual void OnUpdate(const FrameTime& frame_time, const InputState& input) = 0;
        [[nodiscard]] virtual bool ShouldClose() const { return false; }
        virtual void OnStop() {}
    };

    class Application
    {
    public:
        Application(ApplicationDesc desc, IApplicationClient& client);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        int Run();
        void RequestClose();

        [[nodiscard]] const ApplicationDesc& Description() const;
        [[nodiscard]] const InputState& Input() const;
        [[nodiscard]] platform::IWindow& Window();

    private:
        void DispatchEvent(const Event& event);

        ApplicationDesc desc_;
        IApplicationClient& client_;
        std::unique_ptr<platform::IWindow> window_;
        InputState input_;
        Clock clock_;
        bool running_ = false;
    };
}
