#include "Rag/Platform/Win32/Win32Window.h"

#if defined(RAG_PLATFORM_WINDOWS)

#include "Rag/Core/Assert.h"
#include "Rag/Core/Log.h"

#include <Windows.h>
#include <windowsx.h>

#include <string>

namespace rag::platform
{
    namespace
    {
        constexpr const wchar_t* WindowClassName = L"RAG_ENGINE_WIN32_WINDOW";

        std::wstring ToWideString(const std::string& text)
        {
            if (text.empty())
            {
                return {};
            }

            const int required_size = MultiByteToWideChar(
                CP_UTF8,
                0,
                text.data(),
                static_cast<int>(text.size()),
                nullptr,
                0);

            std::wstring result(static_cast<std::size_t>(required_size), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                0,
                text.data(),
                static_cast<int>(text.size()),
                result.data(),
                required_size);
            return result;
        }

        core::KeyCode TranslateKey(WPARAM key)
        {
            using core::KeyCode;

            if (key >= 'A' && key <= 'Z')
            {
                return static_cast<KeyCode>(
                    static_cast<u16>(KeyCode::A) + static_cast<u16>(key - 'A'));
            }

            if (key >= '0' && key <= '9')
            {
                return static_cast<KeyCode>(
                    static_cast<u16>(KeyCode::D0) + static_cast<u16>(key - '0'));
            }

            switch (key)
            {
            case VK_SPACE:
                return KeyCode::Space;
            case VK_OEM_7:
                return KeyCode::Apostrophe;
            case VK_OEM_COMMA:
                return KeyCode::Comma;
            case VK_OEM_MINUS:
                return KeyCode::Minus;
            case VK_OEM_PERIOD:
                return KeyCode::Period;
            case VK_OEM_2:
                return KeyCode::Slash;
            case VK_OEM_1:
                return KeyCode::Semicolon;
            case VK_OEM_PLUS:
                return KeyCode::Equal;
            case VK_ESCAPE:
                return KeyCode::Escape;
            case VK_RETURN:
                return KeyCode::Enter;
            case VK_TAB:
                return KeyCode::Tab;
            case VK_BACK:
                return KeyCode::Backspace;
            case VK_INSERT:
                return KeyCode::Insert;
            case VK_DELETE:
                return KeyCode::Delete;
            case VK_RIGHT:
                return KeyCode::Right;
            case VK_LEFT:
                return KeyCode::Left;
            case VK_DOWN:
                return KeyCode::Down;
            case VK_UP:
                return KeyCode::Up;
            case VK_PRIOR:
                return KeyCode::PageUp;
            case VK_NEXT:
                return KeyCode::PageDown;
            case VK_HOME:
                return KeyCode::Home;
            case VK_END:
                return KeyCode::End;
            case VK_CAPITAL:
                return KeyCode::CapsLock;
            case VK_SCROLL:
                return KeyCode::ScrollLock;
            case VK_NUMLOCK:
                return KeyCode::NumLock;
            case VK_SNAPSHOT:
                return KeyCode::PrintScreen;
            case VK_PAUSE:
                return KeyCode::Pause;
            case VK_F1:
                return KeyCode::F1;
            case VK_F2:
                return KeyCode::F2;
            case VK_F3:
                return KeyCode::F3;
            case VK_F4:
                return KeyCode::F4;
            case VK_F5:
                return KeyCode::F5;
            case VK_F6:
                return KeyCode::F6;
            case VK_F7:
                return KeyCode::F7;
            case VK_F8:
                return KeyCode::F8;
            case VK_F9:
                return KeyCode::F9;
            case VK_F10:
                return KeyCode::F10;
            case VK_F11:
                return KeyCode::F11;
            case VK_F12:
                return KeyCode::F12;
            case VK_LSHIFT:
                return KeyCode::LeftShift;
            case VK_LCONTROL:
                return KeyCode::LeftControl;
            case VK_LMENU:
                return KeyCode::LeftAlt;
            case VK_RSHIFT:
                return KeyCode::RightShift;
            case VK_RCONTROL:
                return KeyCode::RightControl;
            case VK_RMENU:
                return KeyCode::RightAlt;
            default:
                return KeyCode::Unknown;
            }
        }

        WPARAM ResolveModifierVirtualKey(WPARAM key, LPARAM key_data)
        {
            if (key == VK_SHIFT)
            {
                const UINT scan_code =
                    static_cast<UINT>((static_cast<UINT_PTR>(key_data) >> 16u) & 0xffu);
                return static_cast<WPARAM>(
                    MapVirtualKeyW(scan_code, MAPVK_VSC_TO_VK_EX));
            }

            const bool extended =
                (static_cast<UINT_PTR>(key_data) & (static_cast<UINT_PTR>(1) << 24u)) != 0;

            if (key == VK_CONTROL)
            {
                return extended ? VK_RCONTROL : VK_LCONTROL;
            }

            if (key == VK_MENU)
            {
                return extended ? VK_RMENU : VK_LMENU;
            }

            return key;
        }

        core::MouseButton TranslateMouseButton(UINT message, WPARAM w_param)
        {
            using core::MouseButton;

            switch (message)
            {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                return MouseButton::Left;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                return MouseButton::Right;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                return MouseButton::Middle;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
                return GET_XBUTTON_WPARAM(w_param) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
            default:
                return MouseButton::Left;
            }
        }

        LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
        {
            Win32Window* window = nullptr;

            if (message == WM_NCCREATE)
            {
                const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
                window = static_cast<Win32Window*>(create_struct->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            }
            else
            {
                window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            }

            if (window != nullptr)
            {
                i64 result = 0;
                bool handled = false;
                window->HandleMessage(hwnd, message, w_param, l_param, result, handled);
                if (handled)
                {
                    return static_cast<LRESULT>(result);
                }
            }

            return DefWindowProcW(hwnd, message, w_param, l_param);
        }

        void RegisterWindowClass(HINSTANCE instance)
        {
            static bool registered = false;
            if (registered)
            {
                return;
            }

            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(WNDCLASSEXW);
            window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
            window_class.lpfnWndProc = WindowProc;
            window_class.hInstance = instance;
            window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            window_class.hbrBackground = nullptr;
            window_class.lpszClassName = WindowClassName;

            const ATOM atom = RegisterClassExW(&window_class);
            RAG_ASSERT(atom != 0, "RegisterClassExW failed.");

            registered = true;
        }
    }

    Win32Window::Win32Window(const WindowDesc& desc)
        : width_(desc.width),
          height_(desc.height)
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        instance_ = instance;

        RegisterWindowClass(instance);

        RECT window_rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
        AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

        const std::wstring title = ToWideString(desc.title);
        hwnd_ = CreateWindowExW(
            0,
            WindowClassName,
            title.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            nullptr,
            nullptr,
            instance,
            this);

        RAG_ASSERT(hwnd_ != nullptr, "CreateWindowExW failed.");

        ShowWindow(static_cast<HWND>(hwnd_), SW_SHOW);
        UpdateWindow(static_cast<HWND>(hwnd_));

        RAG_LOG_INFO("Created Win32 window ", width_, "x", height_);
    }

    Win32Window::~Win32Window()
    {
        if (hwnd_ != nullptr)
        {
            DestroyWindow(static_cast<HWND>(hwnd_));
            hwnd_ = nullptr;
        }
    }

    void Win32Window::PollEvents()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                should_close_ = true;
                continue;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    void Win32Window::SetEventCallback(WindowEventCallback callback)
    {
        event_callback_ = std::move(callback);
    }

    void Win32Window::SetTitle(const std::string& title)
    {
        const std::wstring wide_title = ToWideString(title);
        SetWindowTextW(static_cast<HWND>(hwnd_), wide_title.c_str());
    }

    void Win32Window::SetFullscreen(bool fullscreen)
    {
        if (fullscreen_ == fullscreen || hwnd_ == nullptr)
        {
            return;
        }

        HWND hwnd = static_cast<HWND>(hwnd_);

        if (fullscreen)
        {
            RECT rect{};
            GetWindowRect(hwnd, &rect);
            windowed_left_ = rect.left;
            windowed_top_ = rect.top;
            windowed_right_ = rect.right;
            windowed_bottom_ = rect.bottom;
            windowed_style_ = GetWindowLongPtrW(hwnd, GWL_STYLE);
            windowed_ex_style_ = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitor_info{};
            monitor_info.cbSize = sizeof(MONITORINFO);
            GetMonitorInfoW(monitor, &monitor_info);

            const LONG_PTR fullscreen_style =
                (static_cast<LONG_PTR>(windowed_style_) & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) |
                static_cast<LONG_PTR>(WS_POPUP) |
                static_cast<LONG_PTR>(WS_VISIBLE);
            const LONG_PTR fullscreen_ex_style =
                static_cast<LONG_PTR>(windowed_ex_style_) &
                ~static_cast<LONG_PTR>(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

            SetWindowLongPtrW(hwnd, GWL_STYLE, fullscreen_style);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, fullscreen_ex_style);

            SetWindowPos(
                hwnd,
                HWND_TOP,
                monitor_info.rcMonitor.left,
                monitor_info.rcMonitor.top,
                monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

            fullscreen_ = true;
            RAG_LOG_INFO("Entered borderless fullscreen.");
            return;
        }

        SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(windowed_style_));
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(windowed_ex_style_));
        SetWindowPos(
            hwnd,
            nullptr,
            windowed_left_,
            windowed_top_,
            windowed_right_ - windowed_left_,
            windowed_bottom_ - windowed_top_,
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        fullscreen_ = false;
        RAG_LOG_INFO("Exited borderless fullscreen.");
    }

    bool Win32Window::ShouldClose() const
    {
        return should_close_;
    }

    bool Win32Window::IsFullscreen() const
    {
        return fullscreen_;
    }

    u32 Win32Window::Width() const
    {
        return width_;
    }

    u32 Win32Window::Height() const
    {
        return height_;
    }

    NativeWindowHandle Win32Window::GetNativeHandle() const
    {
        return NativeWindowHandle{instance_, hwnd_};
    }

    void Win32Window::HandleMessage(
        void* hwnd,
        u32 message,
        u64 w_param,
        i64 l_param,
        i64& result,
        bool& handled)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            result = 1;
            handled = true;
            break;

        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            BeginPaint(static_cast<HWND>(hwnd), &paint);
            EndPaint(static_cast<HWND>(hwnd), &paint);
            result = 0;
            handled = true;
            break;
        }

        case WM_CLOSE:
            should_close_ = true;
            if (event_callback_)
            {
                event_callback_(core::Event::WindowClose());
            }
            result = 0;
            handled = true;
            break;

        case WM_DESTROY:
            if (hwnd == hwnd_)
            {
                hwnd_ = nullptr;
                PostQuitMessage(0);
            }
            result = 0;
            handled = true;
            break;

        case WM_SIZE:
        {
            width_ = static_cast<u32>(LOWORD(l_param));
            height_ = static_cast<u32>(HIWORD(l_param));
            const bool minimized = w_param == SIZE_MINIMIZED;

            if (event_callback_)
            {
                event_callback_(core::Event::WindowResize(width_, height_, minimized));
            }

            result = 0;
            handled = true;
            break;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            const core::KeyCode key = TranslateKey(ResolveModifierVirtualKey(
                static_cast<WPARAM>(w_param),
                static_cast<LPARAM>(l_param)));
            const bool repeat = (l_param & (1ll << 30)) != 0;
            if (event_callback_ && key != core::KeyCode::Unknown)
            {
                event_callback_(core::Event::KeyPressed(key, repeat));
            }
            break;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            const core::KeyCode key = TranslateKey(ResolveModifierVirtualKey(
                static_cast<WPARAM>(w_param),
                static_cast<LPARAM>(l_param)));
            if (event_callback_ && key != core::KeyCode::Unknown)
            {
                event_callback_(core::Event::KeyReleased(key));
            }
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        {
            SetCapture(static_cast<HWND>(hwnd));
            if (event_callback_)
            {
                event_callback_(core::Event::MouseButtonPressed(
                    TranslateMouseButton(message, w_param)));
            }
            break;
        }

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        {
            ReleaseCapture();
            if (event_callback_)
            {
                event_callback_(core::Event::MouseButtonReleased(
                    TranslateMouseButton(message, w_param)));
            }
            break;
        }

        case WM_MOUSEMOVE:
            if (event_callback_)
            {
                event_callback_(core::Event::MouseMoved(
                    GET_X_LPARAM(l_param),
                    GET_Y_LPARAM(l_param)));
            }
            break;

        case WM_MOUSEWHEEL:
            if (event_callback_)
            {
                event_callback_(core::Event::MouseScrolled(
                    0.0f,
                    static_cast<f32>(GET_WHEEL_DELTA_WPARAM(w_param)) / static_cast<f32>(WHEEL_DELTA)));
            }
            break;

        case WM_MOUSEHWHEEL:
            if (event_callback_)
            {
                event_callback_(core::Event::MouseScrolled(
                    static_cast<f32>(GET_WHEEL_DELTA_WPARAM(w_param)) / static_cast<f32>(WHEEL_DELTA),
                    0.0f));
            }
            break;

        default:
            break;
        }
    }

    std::unique_ptr<IWindow> CreatePlatformWindow(const WindowDesc& desc)
    {
        return std::make_unique<Win32Window>(desc);
    }
}

#endif
