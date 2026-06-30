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

    // Optional hook for the raw native window messages, used to let an overlay
    // (e.g. an ImGui backend) inspect input before the engine does. Returning
    // true consumes the message and stops further engine handling; out_result
    // is then returned to the OS. The parameters are the platform-native message
    // values passed through as generic integers so no native/overlay headers
    // leak into the platform abstraction.
    using NativeMessageHook =
        std::function<bool(void* native_window, u32 message, u64 w_param, i64 l_param, i64& out_result)>;

    class IWindow
    {
    public:
        virtual ~IWindow() = default;

        virtual void PollEvents() = 0;
        virtual void SetEventCallback(WindowEventCallback callback) = 0;
        virtual void SetTitle(const std::string& title) = 0;
        virtual void SetFullscreen(bool fullscreen) = 0;

        // No-op by default; platforms with a native message pump override this.
        virtual void SetNativeMessageHook(NativeMessageHook hook) { (void)hook; }

        [[nodiscard]] virtual bool ShouldClose() const = 0;
        [[nodiscard]] virtual bool IsFullscreen() const = 0;
        [[nodiscard]] virtual u32 Width() const = 0;
        [[nodiscard]] virtual u32 Height() const = 0;
        [[nodiscard]] virtual NativeWindowHandle GetNativeHandle() const = 0;
    };

    std::unique_ptr<IWindow> CreatePlatformWindow(const WindowDesc& desc);
}
