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
        void SetNativeMessageHook(NativeMessageHook hook) override;
        void SetTitle(const std::string& title) override;
        void SetFullscreen(bool fullscreen) override;
        void SetCursorCaptured(bool captured) override;
        [[nodiscard]] bool IsCursorCaptured() const override;

        [[nodiscard]] bool ShouldClose() const override;
        [[nodiscard]] bool IsFullscreen() const override;
        [[nodiscard]] bool IsWindowMinimized() const override;
        [[nodiscard]] u32 Width() const override;
        [[nodiscard]] u32 Height() const override;
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;

        void HandleMessage(void* hwnd, u32 message, u64 w_param, i64 l_param, i64& result, bool& handled);

    private:
        // Re-applies the cursor confinement to the current client rectangle
        // (after a move/resize while captured).
        void ApplyCursorClip() const;
        void CenterCursor() const;

        void* instance_ = nullptr;
        void* hwnd_ = nullptr;
        u32 width_ = 0;
        u32 height_ = 0;
        i64 windowed_style_ = 0;
        i64 windowed_ex_style_ = 0;
        i32 windowed_left_ = 0;
        i32 windowed_top_ = 0;
        i32 windowed_right_ = 0;
        i32 windowed_bottom_ = 0;
        bool should_close_ = false;
        bool fullscreen_ = false;
        bool minimized_ = false;
        bool cursor_captured_ = false;
        bool has_absolute_mouse_ = false;
        i32 absolute_mouse_x_ = 0;
        i32 absolute_mouse_y_ = 0;
        WindowEventCallback event_callback_;
        NativeMessageHook native_message_hook_;
    };
}

#endif
