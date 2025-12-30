// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/camera_3d.h
//
// Purpose:
//   Minimal 3D camera for Strata.
//
// Conventions:
//   - Right-handed world space
//   - Y-up world axis by default
//   - Camera forward is derived from yaw/pitch:
//       yaw = 0   => forward = (0, 0, -1)
//       yaw = pi/2=> forward = (1, 0, 0)
//   - Projection uses Vulkan depth range [0, 1] via base::math::perspective_rh_zo
// -----------------------------------------------------------------------------

#pragma once

#include <algorithm> // std::clamp
#include <cmath>     // std::sin, std::cos

#include "strata/base/math.h"

namespace strata::gfx::renderer
{

class Camera3D
{
  public:
    // World-space camera position.
    base::math::Vec3 position{0.0f, 0.0f, 2.0f};

    // Orientation in radians.
    float yaw_radians{0.0f};
    float pitch_radians{0.0f};

    // Camera lens.
    float fov_y_radians{base::math::deg_to_rad(60.0f)};
    float near_z{0.1f};
    float far_z{1000.0f};

    // World up axis.
    base::math::Vec3 world_up{0.0f, 1.0f, 0.0f};

    // Set yaw/pitch (radians). Pitch is clamped to avoid degeneracy.
    void set_yaw_pitch(float yaw, float pitch) noexcept
    {
        yaw_radians   = yaw;
        pitch_radians = pitch;
        clamp_pitch();
    }

    // Adjust yaw/pitch by deltas (radians).
    void add_yaw_pitch(float yaw_delta, float pitch_delta) noexcept
    {
        yaw_radians += yaw_delta;
        pitch_radians += pitch_delta;
        clamp_pitch();
    }

    [[nodiscard]]
    base::math::Vec3 forward() const noexcept
    {
        // yaw around world Y, pitch around camera local X.
        float const cy = std::cos(yaw_radians);
        float const sy = std::sin(yaw_radians);
        float const cp = std::cos(pitch_radians);
        float const sp = std::sin(pitch_radians);

        // yaw=0, pitch=0 => forward = (0,0,-1)
        base::math::Vec3 f{
            sy * cp,
            sp,
            -cy * cp,
        };

        return base::math::normalize(f);
    }

    [[nodiscard]]
    base::math::Vec3 right() const noexcept
    {
        // Right-handed: right = forward x up.
        return base::math::normalize(base::math::cross(forward(), world_up));
    }

    [[nodiscard]]
    base::math::Vec3 up() const noexcept
    {
        // Recompute orthonormal up from right and forward.
        base::math::Vec3 const f = forward();
        base::math::Vec3 const r = right();
        return base::math::normalize(base::math::cross(r, f));
    }

    [[nodiscard]]
    base::math::Mat4 view() const noexcept
    {
        base::math::Vec3 const f = forward();
        return base::math::look_at_rh(position, position + f, world_up);
    }

    [[nodiscard]]
    base::math::Mat4 proj(float aspect, bool flip_y_for_vulkan_viewport = true) const noexcept
    {
        return base::math::perspective_rh_zo(fov_y_radians,
                                             aspect,
                                             near_z,
                                             far_z,
                                             flip_y_for_vulkan_viewport);
    }

    [[nodiscard]]
    base::math::Mat4 view_proj(float aspect, bool flip_y_for_vulkan_viewport = true) const noexcept
    {
        return base::math::mul(proj(aspect, flip_y_for_vulkan_viewport), view());
    }

  private:
    void clamp_pitch() noexcept
    {
        // Avoid forward becoming parallel to up (gimbal singularity).
        // Keep a small epsilon from +/- 90 degrees.
        float const limit = base::math::half_pi - 0.001f;
        pitch_radians     = std::clamp(pitch_radians, -limit, limit);
    }
};

} // namespace strata::gfx::renderer
