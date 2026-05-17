#pragma once

#include "Rag/Core/Base.h"

#include <variant>

namespace rag::core
{
    enum class EventType : u8
    {
        None,
        WindowClose,
        WindowResize,
        KeyPressed,
        KeyReleased,
        MouseButtonPressed,
        MouseButtonReleased,
        MouseMoved,
        MouseScrolled
    };

    struct WindowCloseEvent
    {
    };

    struct WindowResizeEvent
    {
        u32 width = 0;
        u32 height = 0;
        bool minimized = false;
    };

    enum class KeyCode : u16
    {
        Unknown = 0,

        Space,
        Apostrophe,
        Comma,
        Minus,
        Period,
        Slash,

        D0,
        D1,
        D2,
        D3,
        D4,
        D5,
        D6,
        D7,
        D8,
        D9,

        Semicolon,
        Equal,

        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,

        Escape,
        Enter,
        Tab,
        Backspace,
        Insert,
        Delete,
        Right,
        Left,
        Down,
        Up,
        PageUp,
        PageDown,
        Home,
        End,
        CapsLock,
        ScrollLock,
        NumLock,
        PrintScreen,
        Pause,

        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        LeftShift,
        LeftControl,
        LeftAlt,
        RightShift,
        RightControl,
        RightAlt,

        Count
    };

    enum class MouseButton : u8
    {
        Left = 0,
        Right,
        Middle,
        X1,
        X2,
        Count
    };

    struct KeyEvent
    {
        KeyCode key = KeyCode::Unknown;
        bool repeat = false;
    };

    struct MouseButtonEvent
    {
        MouseButton button = MouseButton::Left;
    };

    struct MouseMoveEvent
    {
        i32 x = 0;
        i32 y = 0;
    };

    struct MouseScrollEvent
    {
        f32 delta_x = 0.0f;
        f32 delta_y = 0.0f;
    };

    using EventPayload = std::variant<
        WindowCloseEvent,
        WindowResizeEvent,
        KeyEvent,
        MouseButtonEvent,
        MouseMoveEvent,
        MouseScrollEvent>;

    struct Event
    {
        EventType type = EventType::None;
        EventPayload payload = WindowCloseEvent{};

        static Event WindowClose()
        {
            return Event{EventType::WindowClose, WindowCloseEvent{}};
        }

        static Event WindowResize(u32 width, u32 height, bool minimized)
        {
            return Event{EventType::WindowResize, WindowResizeEvent{width, height, minimized}};
        }

        static Event KeyPressed(KeyCode key, bool repeat)
        {
            return Event{EventType::KeyPressed, KeyEvent{key, repeat}};
        }

        static Event KeyReleased(KeyCode key)
        {
            return Event{EventType::KeyReleased, KeyEvent{key, false}};
        }

        static Event MouseButtonPressed(MouseButton button)
        {
            return Event{EventType::MouseButtonPressed, MouseButtonEvent{button}};
        }

        static Event MouseButtonReleased(MouseButton button)
        {
            return Event{EventType::MouseButtonReleased, MouseButtonEvent{button}};
        }

        static Event MouseMoved(i32 x, i32 y)
        {
            return Event{EventType::MouseMoved, MouseMoveEvent{x, y}};
        }

        static Event MouseScrolled(f32 delta_x, f32 delta_y)
        {
            return Event{EventType::MouseScrolled, MouseScrollEvent{delta_x, delta_y}};
        }
    };
}
