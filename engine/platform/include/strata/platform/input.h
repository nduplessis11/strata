// -----------------------------------------------------------------------------
// engine/platform/include/strata/platform/input.h
//
// Purpose:
//   Minimal cross-platform input state snapshot for Strata.
//
// Design (v1):
//   - Owned by platform::Window (no globals).
//   - poll_events() resets per-frame deltas (mouse, wheel).
//   - Keys/buttons are "current down" state.
//   - Enough for camera controls + basic gameplay.
//
// Notes:
//   - This is intentionally tiny and not a full input system.
//   - Future: add edge detection, text input, gamepad, remapping, etc.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace strata::platform
{

enum class Key : std::uint8_t
{
    W,
    A,
    S,
    D,

    Space,
    Ctrl,
    Shift,
    Escape,

    Count
};

enum class MouseButton : std::uint8_t
{
    Left,
    Right,
    Middle,

    Count
};

class InputState
{
  public:
    // Called once per frame by Window::poll_events() before pumping OS events.
    void begin_frame() noexcept
    {
        mouse_dx_    = 0.0f;
        mouse_dy_    = 0.0f;
        wheel_delta_ = 0.0f;
    }

    // Clear all key/button state and per-frame deltas.
    void clear() noexcept
    {
        keys_.fill(false);
        mouse_buttons_.fill(false);
        mouse_pos_valid_ = false;
        mouse_x_         = 0;
        mouse_y_         = 0;
        begin_frame();
    }

    void set_focused(bool focused) noexcept
    {
        focused_ = focused;
        if (!focused_)
        {
            // Prevent stuck keys when alt-tab / focus loss.
            clear();
        }
        else
        {
            // Start clean on focus gain.
            begin_frame();
        }
    }

    [[nodiscard]]
    bool focused() const noexcept
    {
        return focused_;
    }

    void set_key(Key key, bool down) noexcept
    {
        keys_[to_index(key)] = down;
    }

    [[nodiscard]]
    bool key_down(Key key) const noexcept
    {
        return keys_[to_index(key)];
    }

    void set_mouse_button(MouseButton b, bool down) noexcept
    {
        mouse_buttons_[to_index(b)] = down;
    }

    [[nodiscard]]
    bool mouse_down(MouseButton b) const noexcept
    {
        return mouse_buttons_[to_index(b)];
    }

    void add_mouse_delta(float dx, float dy) noexcept
    {
        mouse_dx_ += dx;
        mouse_dy_ += dy;
    }

    [[nodiscard]]
    float mouse_delta_x() const noexcept
    {
        return mouse_dx_;
    }

    [[nodiscard]]
    float mouse_delta_y() const noexcept
    {
        return mouse_dy_;
    }

    void add_wheel_delta(float delta) noexcept
    {
        wheel_delta_ += delta;
    }

    [[nodiscard]]
    float wheel_delta() const noexcept
    {
        return wheel_delta_;
    }

    // Absolute mouse position in *client/window coordinates* (origin top-left).
    // This is useful for editor picking, UI hit-testing, etc.
    //
    // Platform backends should call set_mouse_pos() on motion events.
    void set_mouse_pos(std::int32_t x, std::int32_t y) noexcept
    {
        mouse_x_         = x;
        mouse_y_         = y;
        mouse_pos_valid_ = true;
    }

    [[nodiscard]]
    bool mouse_pos_valid() const noexcept
    {
        return mouse_pos_valid_;
    }

    [[nodiscard]]
    std::int32_t mouse_x() const noexcept
    {
        return mouse_x_;
    }

    [[nodiscard]]
    std::int32_t mouse_y() const noexcept
    {
        return mouse_y_;
    }

  private:
    static constexpr std::size_t to_index(Key k) noexcept
    {
        return static_cast<std::size_t>(k);
    }
    static constexpr std::size_t to_index(MouseButton b) noexcept
    {
        return static_cast<std::size_t>(b);
    }

    std::array<bool, static_cast<std::size_t>(Key::Count)>         keys_{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> mouse_buttons_{};

    float mouse_dx_{0.0f};
    float mouse_dy_{0.0f};
    float wheel_delta_{0.0f};

    bool         mouse_pos_valid_{false};
    std::int32_t mouse_x_{0};
    std::int32_t mouse_y_{0};

    bool focused_{true};
};

} // namespace strata::platform