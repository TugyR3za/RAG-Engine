#pragma once

#include "Rag/Platform/Window.h"

#if defined(RAG_PLATFORM_WINDOWS)

#include <string>

namespace rag::platform
{
    class Win32Window final : public IWindow
    {
    public:
        explicit Win32Window(const WindowDesc& desc);
        ~Win32Window() override;

        Win32Window(const Win32Window&) = delete;
        Win32Window& operator=(const Win32Window&) = delete;

        void PollEvents() override;
        void SetEventCallback(WindowEventCallback callback) override;
        void SetTitle(const std::string& title) override;

        [[nodiscard]] bool ShouldClose() const override;
        [[nodiscard]] u32 Width() const override;
        [[nodiscard]] u32 Height() const override;
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;

        void HandleMessage(void* hwnd, u32 message, u64 w_param, i64 l_param, i64& result, bool& handled);

    private:
        void* instance_ = nullptr;
        void* hwnd_ = nullptr;
        u32 width_ = 0;
        u32 height_ = 0;
        bool should_close_ = false;
        WindowEventCallback event_callback_;
    };
}

#endif
