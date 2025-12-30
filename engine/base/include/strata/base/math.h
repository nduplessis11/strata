// -----------------------------------------------------------------------------
// engine/base/include/strata/base/math.h
//
// Purpose:
//   Minimal math types and helpers for Strata.
//
// Conventions (v1):
//   - Right-handed coordinate system
//   - Column-major matrices (m[column][row]) compatible with GLSL default layout
//   - Vector is treated as a column vector: v' = M * v
//   - Projection uses Vulkan-style depth range: Z in [0, 1] (ZO)
//
// Notes:
//   - This is intentionally tiny and dependency-free.
//   - This is not a full math library; add functionality incrementally as needed.
// -----------------------------------------------------------------------------

#pragma once

#include <algorithm> // std::clamp
#include <cmath>     // std::sin, std::cos, std::tan, std::sqrt

namespace strata::base::math
{

inline constexpr float pi      = 3.14159265358979323846f;
inline constexpr float two_pi  = pi * 2.0f;
inline constexpr float half_pi = pi * 0.5f;

[[nodiscard]]
inline constexpr float deg_to_rad(float degrees) noexcept
{
    return degrees * (pi / 180.0f);
}

struct Vec3
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float xx, float yy, float zz) noexcept
          : x(xx)
          , y(yy)
          , z(zz)
    {
    }
};

struct Vec4
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{0.0f};

    constexpr Vec4() noexcept = default;
    constexpr Vec4(float xx, float yy, float zz, float ww) noexcept
          : x(xx)
          , y(yy)
          , z(zz)
          , w(ww)
    {
    }
    constexpr Vec4(Vec3 v, float ww) noexcept
          : x(v.x)
          , y(v.y)
          , z(v.z)
          , w(ww)
    {
    }
};

[[nodiscard]]
inline constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept
{
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]]
inline constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept
{
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]]
inline constexpr Vec3 operator*(Vec3 v, float s) noexcept
{
    return Vec3{v.x * s, v.y * s, v.z * s};
}
[[nodiscard]]
inline constexpr Vec3 operator*(float s, Vec3 v) noexcept
{
    return v * s;
}
[[nodiscard]]
inline constexpr Vec3 operator/(Vec3 v, float s) noexcept
{
    return Vec3{v.x / s, v.y / s, v.z / s};
}

[[nodiscard]]
inline constexpr float dot(Vec3 a, Vec3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]]
inline constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept
{
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]]
inline float length(Vec3 v) noexcept
{
    return std::sqrt(dot(v, v));
}

[[nodiscard]]
inline Vec3 normalize(Vec3 v) noexcept
{
    float const len = length(v);
    if (len <= 0.0f)
        return Vec3{0.0f, 0.0f, 0.0f};
    return v / len;
}

// 4x4 matrix in column-major order: m[column][row].
// This matches GLSL default (column-major) and is convenient for UBO upload.
struct alignas(16) Mat4
{
    float m[4][4]{};

    [[nodiscard]]
    constexpr float* data() noexcept
    {
        return &m[0][0];
    }
    [[nodiscard]]
    constexpr float const* data() const noexcept
    {
        return &m[0][0];
    }

    [[nodiscard]]
    static constexpr Mat4 identity() noexcept
    {
        Mat4 out{};
        out.m[0][0] = 1.0f;
        out.m[1][1] = 1.0f;
        out.m[2][2] = 1.0f;
        out.m[3][3] = 1.0f;
        return out;
    }

    [[nodiscard]]
    inline Mat4 mul(Mat4 const& a, Mat4 const& b) noexcept
    {
        // Column-major multiplication: out = a * b
        Mat4 out{};

        for (int c = 0; c < 4; ++c) // column
        {
            for (int r = 0; r < 4; ++r) // row
            {
                // clang-format off
                out.m[c][r] = 
                    a.m[0][r] * b.m[c][0] +
                    a.m[1][r] * b.m[c][1] +
                    a.m[2][r] * b.m[c][2] +
                    a.m[3][r] * b.m[c][3];
                // clang-format on
            }
        }

        return out;
    }
};

[[nodiscard]]
inline Vec4 mul(Mat4 const& m, Vec4 const& v) noexcept
{
    // v' = M * v, column vector.
    return Vec4{m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0] * v.w,
                m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1] * v.w,
                m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2] * v.w,
                m.m[0][3] * v.x + m.m[1][3] * v.y + m.m[2][3] * v.z + m.m[3][3] * v.w};
}

// Right handed look-at view matrix.
// Camera looks toward (target - eye). View space looks down -Z.
[[nodiscard]]
inline Mat4 look_at_rh(Vec3 eye, Vec3 target, Vec3 up) noexcept
{
    Vec3 const f = normalize(target - eye); // forward (world)
    Vec3 const s = normalize(cross(f, up)); // right
    Vec3 const u = cross(s, f);             // true up

    Mat4 out = Mat4::identity();

    // Basis (columns): right, up, -forward
    out.m[0][0] = s.x;
    out.m[0][1] = s.y;
    out.m[0][2] = s.z;
    out.m[0][3] = 0.0f;

    out.m[1][0] = u.x;
    out.m[1][1] = u.y;
    out.m[1][2] = u.z;
    out.m[1][3] = 0.0f;

    out.m[2][0] = -f.x;
    out.m[2][1] = -f.y;
    out.m[2][2] = -f.z;
    out.m[2][3] = 0.0f;

    // Translation (fourth column)
    out.m[3][0] = -dot(s, eye);
    out.m[3][1] = -dot(u, eye);
    out.m[3][2] = dot(f, eye); // because basis is -f
    out.m[3][3] = 1.0f;

    return out;
}

// Right-handed perspective projection with Vulkan depth range [0, 1] (ZO).
//
// flip_y_for_vulkan_viewport:
//   If our VkViewport uses a *positive* height (like Strata currently does),
//   Vulkan's screen-space Y ends up inverted compared to typical "Y-up" math.
//   A common approach is to flip Y in projection by negating m[1][1].
//   If we later switch to negative viewport height to flip (VkViewport.height < 0),
//   pass flip_y_for_vulkan_viewport = false.
[[nodiscard]]
inline Mat4 perspective_rh_zo(float fov_y_radians,
                              float aspect,
                              float near_z,
                              float far_z,
                              bool  flip_y_for_vulkan_viewport = true) noexcept
{
    // Defensive guards (avoid NaNs/div-by-zero).
    if (aspect <= 0.0f)
        aspect = 1.0f;

    if (near_z <= 0.0f)
        near_z = 0.001f;

    if (far_z <= near_z + 0.0001f)
        far_z = near_z + 1.0f;

    float const f = 1.0f / std::tan(fov_y_radians * 0.5f);

    Mat4 out{};

    out.m[0][0] = f / aspect;
    out.m[1][1] = flip_y_for_vulkan_viewport ? -f : f;

    // RH, ZO (DirectX/Vulkan style)
    out.m[2][2] = far_z / (near_z - far_z);
    out.m[2][3] = -1.0f;

    out.m[3][2] = (far_z * near_z) / (near_z - far_z);

    return out;
}

} // namespace strata::base::math