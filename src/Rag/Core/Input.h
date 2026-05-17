#pragma once

#include "Rag/Core/Event.h"

#include <array>

namespace rag::core
{
    struct ButtonState
    {
        bool down = false;
        bool pressed = false;
        bool released = false;
    };

    class InputState
    {
    public:
        void BeginFrame();
        void ApplyEvent(const Event& event);

        [[nodiscard]] bool IsKeyDown(KeyCode key) const;
        [[nodiscard]] bool WasKeyPressed(KeyCode key) const;
        [[nodiscard]] bool WasKeyReleased(KeyCode key) const;

        [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const;
        [[nodiscard]] bool WasMouseButtonPressed(MouseButton button) const;
        [[nodiscard]] bool WasMouseButtonReleased(MouseButton button) const;

        [[nodiscard]] i32 MouseX() const;
        [[nodiscard]] i32 MouseY() const;
        [[nodiscard]] f32 ScrollDeltaX() const;
        [[nodiscard]] f32 ScrollDeltaY() const;

    private:
        static constexpr std::size_t KeyCount = static_cast<std::size_t>(KeyCode::Count);
        static constexpr std::size_t MouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

        std::array<ButtonState, KeyCount> keys_{};
        std::array<ButtonState, MouseButtonCount> mouse_buttons_{};
        i32 mouse_x_ = 0;
        i32 mouse_y_ = 0;
        f32 scroll_delta_x_ = 0.0f;
        f32 scroll_delta_y_ = 0.0f;
    };
}
