// engine/platform/include/strata/platform/wsi_handle.h
//
// OS-agnostic description of the native windowing state needed to create a Vulkan surface.
// Strongly-typed wrappers + std::variant. No OS or Vulkan headers here.

#pragma once

#include <cstdint>
#include <variant>

namespace strata::platform::wsi {

    // -------------------- Win32 --------------------

    struct Win32Instance {
        std::uintptr_t value{};
        explicit constexpr operator bool() const noexcept { return value != 0; }
    };

    struct Win32Window {
        std::uintptr_t value{};
        explicit constexpr operator bool() const noexcept { return value != 0; }
    };

    struct Win32 {
        Win32Instance instance{};
        Win32Window   window{};

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return static_cast<bool>(instance) && static_cast<bool>(window);
        }
    };

    // -------------------- X11 --------------------

    struct X11Display {
        std::uintptr_t value{};  // reinterpret_cast from Display*
        explicit constexpr operator bool() const noexcept { return value != 0; }
    };

    struct X11Window {
        std::uint64_t value{};   // XID is 32-bit; 64-bit storage is safe
        explicit constexpr operator bool() const noexcept { return value != 0; }
    };

    struct X11 {
        X11Display display{};
        X11Window  window{};

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return static_cast<bool>(display) && static_cast<bool>(window);
        }
    };

    // -------------------- Wayland --------------------

    struct WaylandDisplay {
        std::uintptr_t value{};  // reinterpret_cast from wl_display*
        explicit constexpr operator bool() const noexcept { return value != 0; }
    };

    struct WaylandSurface {
        std::uintptr_t value{};  // reinterpret_cast from wl_surface*
        explicit constexpr operator bool() const noexcept { return value != 0; }
    };

    struct Wayland {
        WaylandDisplay display{};
        WaylandSurface surface{};

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return static_cast<bool>(display) && static_cast<bool>(surface);
        }
    };

} // namespace strata::platform::wsi

namespace strata::platform {

    // Type-safe tagged union for window system integration.
    // Only one alternative (Win32 / X11 / Wayland) is active at a time.
    using WsiHandle = std::variant<
        wsi::Win32,
        wsi::X11,
        wsi::Wayland>;

} // namespace strata::platform
