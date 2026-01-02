// -----------------------------------------------------------------------------
// engine/core/include/strata/core/action_map.h
//
// Purpose:
//   Minimal input action mapping for Strata.
//
// PR9:
//   - Provide a tiny layer between platform raw input and gameplay logic.
//   - Used by arcade_shooter to drive camera yaw/pitch + WASD movement.
//
// Notes:
//   - This is intentionally small. No rebinding system yet.
//   - Future: add multiple devices, chorded bindings, edge detection, etc.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "strata/platform/input.h"

namespace strata::core
{

enum class Action : std::uint8_t
{
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,

    Sprint,
    Exit,

    Count
};

class ActionMap
{
  public:
    void update(platform::InputState const& input) noexcept;

    [[nodiscard]] bool  down(Action a) const noexcept;
    [[nodiscard]] float look_x() const noexcept;
    [[nodiscard]] float look_y() const noexcept;

  private:
    static constexpr std::size_t to_index(Action a) noexcept
    {
        return static_cast<std::size_t>(a);
    }

    std::array<bool, static_cast<std::size_t>(Action::Count)> down_{};

    float look_x_{0.0f};
    float look_y_{0.0f};
};

} // namespace strata::core