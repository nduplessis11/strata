// -----------------------------------------------------------------------------
// engine/core/src/action_map.cpp
//
// Purpose:
//   Minimal action map implementation.
//
// PR9:
//   - Hard-coded default bindings for WASD + mouse look.
// -----------------------------------------------------------------------------

#include "strata/core/action_map.h"

namespace strata::core
{

void ActionMap::update(platform::InputState const& input) noexcept
{
    // Reset actions each frame (digital "is down" dampled from input state).
    down_.fill(false);

    if (!input.focused())
    {
        look_x_ = 0.0f;
        look_y_ = 0.0f;
        return;
    }

    // Mouse look axes (raw per-frame deltas).
    look_x_ = input.mouse_delta_x();
    look_y_ = input.mouse_delta_y();

    // Digital bindings (hard-coded v1).
    down_[to_index(Action::MoveForward)] = input.key_down(platform::Key::W);
    down_[to_index(Action::MoveBack)]    = input.key_down(platform::Key::S);
    down_[to_index(Action::MoveLeft)]    = input.key_down(platform::Key::A);
    down_[to_index(Action::MoveRight)]   = input.key_down(platform::Key::D);

    down_[to_index(Action::MoveUp)]   = input.key_down(platform::Key::Space);
    down_[to_index(Action::MoveDown)] = input.key_down(platform::Key::Ctrl);

    down_[to_index(Action::Sprint)] = input.key_down(platform::Key::Shift);
    down_[to_index(Action::Exit)]   = input.key_down(platform::Key::Escape);
}

bool ActionMap::down(Action a) const noexcept
{
    return down_[to_index(a)];
}

float ActionMap::look_x() const noexcept
{
    return look_x_;
}

float ActionMap::look_y() const noexcept
{
    return look_y_;
}

} // namespace strata::core