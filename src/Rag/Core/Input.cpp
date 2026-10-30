#include "Rag/Core/Input.h"

#include <algorithm>

namespace rag::core
{
    namespace
    {
        template <typename TEnum>
        std::size_t ToIndex(TEnum value)
        {
            return static_cast<std::size_t>(value);
        }

        void Press(ButtonState& state)
        {
            state.pressed = !state.down;
            state.down = true;
        }

        void Release(ButtonState& state)
        {
            state.released = state.down;
            state.down = false;
        }
    }

    void InputState::BeginFrame()
    {
        for (ButtonState& state : keys_)
        {
            state.pressed = false;
            state.released = false;
        }

        for (ButtonState& state : mouse_buttons_)
        {
            state.pressed = false;
            state.released = false;
        }

        scroll_delta_x_ = 0.0f;
        scroll_delta_y_ = 0.0f;
        raw_mouse_delta_x_ = 0;
        raw_mouse_delta_y_ = 0;
    }

    void InputState::ReleaseAll()
    {
        for (ButtonState& state : keys_)
        {
            Release(state);
        }

        for (ButtonState& state : mouse_buttons_)
        {
            Release(state);
        }
    }

    void InputState::ApplyEvent(const Event& event)
    {
        switch (event.type)
        {
        case EventType::KeyPressed:
        {
            const auto& key_event = std::get<KeyEvent>(event.payload);
            const std::size_t index = ToIndex(key_event.key);
            if (index < keys_.size())
            {
                Press(keys_[index]);
            }
            break;
        }
        case EventType::KeyReleased:
        {
            const auto& key_event = std::get<KeyEvent>(event.payload);
            const std::size_t index = ToIndex(key_event.key);
            if (index < keys_.size())
            {
                Release(keys_[index]);
            }
            break;
        }
        case EventType::MouseButtonPressed:
        {
            const auto& mouse_event = std::get<MouseButtonEvent>(event.payload);
            const std::size_t index = ToIndex(mouse_event.button);
            if (index < mouse_buttons_.size())
            {
                Press(mouse_buttons_[index]);
            }
            break;
        }
        case EventType::MouseButtonReleased:
        {
            const auto& mouse_event = std::get<MouseButtonEvent>(event.payload);
            const std::size_t index = ToIndex(mouse_event.button);
            if (index < mouse_buttons_.size())
            {
                Release(mouse_buttons_[index]);
            }
            break;
        }
        case EventType::MouseMoved:
        {
            const auto& move_event = std::get<MouseMoveEvent>(event.payload);
            mouse_x_ = move_event.x;
            mouse_y_ = move_event.y;
            break;
        }
        case EventType::MouseScrolled:
        {
            const auto& scroll_event = std::get<MouseScrollEvent>(event.payload);
            scroll_delta_x_ += scroll_event.delta_x;
            scroll_delta_y_ += scroll_event.delta_y;
            break;
        }
        case EventType::MouseRawMotion:
        {
            const auto& motion_event = std::get<MouseRawMotionEvent>(event.payload);
            raw_mouse_delta_x_ += motion_event.delta_x;
            raw_mouse_delta_y_ += motion_event.delta_y;
            break;
        }
        case EventType::WindowFocus:
        {
            const auto& focus_event = std::get<WindowFocusEvent>(event.payload);
            focused_ = focus_event.focused;
            if (!focused_)
            {
                // Key-up for anything held now goes to the other window, so the
                // key would otherwise stay "down" here forever (stuck movement).
                ReleaseAll();
                raw_mouse_delta_x_ = 0;
                raw_mouse_delta_y_ = 0;
            }
            break;
        }
        default:
            break;
        }
    }

    bool InputState::IsKeyDown(KeyCode key) const
    {
        const std::size_t index = ToIndex(key);
        return index < keys_.size() && keys_[index].down;
    }

    bool InputState::WasKeyPressed(KeyCode key) const
    {
        const std::size_t index = ToIndex(key);
        return index < keys_.size() && keys_[index].pressed;
    }

    bool InputState::WasKeyReleased(KeyCode key) const
    {
        const std::size_t index = ToIndex(key);
        return index < keys_.size() && keys_[index].released;
    }

    bool InputState::IsMouseButtonDown(MouseButton button) const
    {
        const std::size_t index = ToIndex(button);
        return index < mouse_buttons_.size() && mouse_buttons_[index].down;
    }

    bool InputState::WasMouseButtonPressed(MouseButton button) const
    {
        const std::size_t index = ToIndex(button);
        return index < mouse_buttons_.size() && mouse_buttons_[index].pressed;
    }

    bool InputState::WasMouseButtonReleased(MouseButton button) const
    {
        const std::size_t index = ToIndex(button);
        return index < mouse_buttons_.size() && mouse_buttons_[index].released;
    }

    i32 InputState::MouseX() const
    {
        return mouse_x_;
    }

    i32 InputState::MouseY() const
    {
        return mouse_y_;
    }

    f32 InputState::ScrollDeltaX() const
    {
        return scroll_delta_x_;
    }

    f32 InputState::ScrollDeltaY() const
    {
        return scroll_delta_y_;
    }

    i32 InputState::RawMouseDeltaX() const
    {
        return raw_mouse_delta_x_;
    }

    i32 InputState::RawMouseDeltaY() const
    {
        return raw_mouse_delta_y_;
    }

    bool InputState::HasFocus() const
    {
        return focused_;
    }
}
