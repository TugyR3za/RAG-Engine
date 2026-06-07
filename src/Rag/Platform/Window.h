#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Core/Event.h"

#include <functional>
#include <memory>
#include <string>

namespace rag::platform
{
    struct WindowDesc
    {
        std::string title = "RAG Engine";
        u32 width = 1280;
        u32 height = 720;
    };

    struct NativeWindowHandle
    {
        void* instance = nullptr;
        void* window = nullptr;
    };

    using WindowEventCallback = std::function<void(const core::Event&)>;

    class IWindow
    {
    public:
        virtual ~IWindow() = default;

        virtual void PollEvents() = 0;
        virtual void SetEventCallback(WindowEventCallback callback) = 0;
        virtual void SetTitle(const std::string& title) = 0;
        virtual void SetFullscreen(bool fullscreen) = 0;

        [[nodiscard]] virtual bool ShouldClose() const = 0;
        [[nodiscard]] virtual bool IsFullscreen() const = 0;
        [[nodiscard]] virtual u32 Width() const = 0;
        [[nodiscard]] virtual u32 Height() const = 0;
        [[nodiscard]] virtual NativeWindowHandle GetNativeHandle() const = 0;
    };

    std::unique_ptr<IWindow> CreatePlatformWindow(const WindowDesc& desc);
}
