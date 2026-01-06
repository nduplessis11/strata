// -----------------------------------------------------------------------------
// engine/core/include/strata/core/application.h
//
// Purpose:
//   Engine-level Application wrapper. Owns platform window + graphics bring-up,
//   and drives the main loop.
//
// Design goals (C++23, safe-by-default):
//   - No partially-initialized Application objects.
//   - Creation returns std::expected<...> with a clear error.
//   - Accessors return references (no nullable pointers).
//   - Implementation hidden via PIMPL to reduce header dependencies.
// -----------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>

#include "strata/gfx/renderer/renderer.h"
#include "strata/gfx/rhi/gpu_device.h"
#include "strata/platform/window.h"

namespace strata::base
{
class Diagnostics;
}

namespace strata::core
{

struct FrameContext
{
    std::uint64_t frame_index{};
    double        delta_seconds{};
};

struct ApplicationConfig
{
    // Window creation (size, title, etc.)
    strata::platform::WindowDesc window_desc{};

    // Which backend to use (Vulkan for now)
    strata::gfx::rhi::DeviceCreateInfo device{};

    // Swapchain defaults (format/vsync). Size will be set from framebuffer size at runtime.
    strata::gfx::rhi::SwapchainDesc swapchain_desc{};

    // Simple CPI throttle
    bool                      throttle_cpu{true};
    std::chrono::milliseconds throttle_sleep{1};
};

enum class ApplicationError : std::uint8_t
{
    WindowCreateFailed,
    DeviceCreateFailed,
    SwapchainCreateFailed,
    RendererCreateFailed,
};

[[nodiscard]] constexpr std::string_view to_string(ApplicationError e) noexcept
{
    switch (e)
    {
    case ApplicationError::WindowCreateFailed:
        return "WindowCreateFailed";
    case ApplicationError::DeviceCreateFailed:
        return "DeviceCreateFailed";
    case ApplicationError::SwapchainCreateFailed:
        return "SwapchainCreateFailed";
    case ApplicationError::RendererCreateFailed:
        return "RendererCreateFailed";
    }
    return "Unknown";
}

class Application
{
  public:
    using TickFn = std::function<void(Application&, FrameContext const&)>;

    [[nodiscard]]
    static std::expected<Application, ApplicationError> create(ApplicationConfig config = {});

    std::int16_t run(TickFn tick = {});
    void         request_exit() noexcept;

    // --- Accessors -----------------------------------------------------------------
    //
    // Const-propagating accessors: constness of Application controls whether subsystems are
    // returned as T& or T const&.

    [[nodiscard]] strata::platform::Window&       window() noexcept;
    [[nodiscard]] strata::platform::Window const& window() const noexcept;

    [[nodiscard]] strata::gfx::rhi::IGpuDevice&       device() noexcept;
    [[nodiscard]] strata::gfx::rhi::IGpuDevice const& device() const noexcept;

    [[nodiscard]] strata::gfx::rhi::SwapchainHandle swapchain() const noexcept;

    [[nodiscard]] strata::gfx::renderer::Renderer&       renderer() noexcept;
    [[nodiscard]] strata::gfx::renderer::Renderer const& renderer() const noexcept;

    [[nodiscard]] strata::base::Diagnostics&       diagnostics() noexcept;
    [[nodiscard]] strata::base::Diagnostics const& diagnostics() const noexcept;

    [[nodiscard]] ApplicationConfig const& config() const noexcept;

  private:
    struct Impl;

    struct ImplDeleter
    {
        void operator()(Impl*) const noexcept;
    };

    explicit Application(std::unique_ptr<Impl, ImplDeleter> impl) noexcept
          : impl_(std::move(impl))
    {
    }

    std::unique_ptr<Impl, ImplDeleter> impl_{nullptr};
};

} // namespace strata::core
