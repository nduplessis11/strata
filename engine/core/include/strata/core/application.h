// -----------------------------------------------------------------------------
// engine/core/include/strata/core/application.h
//
// Purpose:
//   Engine-level Application wrapper. Owns platform window + graphics bring-up,
//   and drives the main loop.
//
// Notes:
//   - This is intentionally minimal: 1 window, 1 device, 1 swapchain, 1 renderer.
//   - The game can supply a per-frame tick callback.
//   - Rendering uses the existing Render2D frontend for now.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <chrono>
#include <functional>

#include "strata/platform/window.h"
#include "strata/gfx/rhi/gpu_device.h"
#include "strata/gfx/renderer/render_2d.h"
#include <expected>

namespace strata::core {

    struct FrameContext {
        std::uint64_t frame_index{};
        double        delta_seconds{};
    };

    struct ApplicationConfig {
        // Window creation (size, title, etc.)
        strata::platform::WindowDesc window_desc{};

        // Which backend to use (Vulkan for now)
        strata::gfx::rhi::DeviceCreateInfo device{};

        // Swapchain defaults (format/vsync). Size will be set from framebuffer size at runtime.
        strata::gfx::rhi::SwapchainDesc swapchain_desc{};

        // Simple CPI throttle
        bool throttle_cpu{ true };
        std::chrono::milliseconds throttle_sleep{ 1 };
    };

    enum class ApplicationError : std::uint8_t {
        WindowCreateFailed,
        DeviceCreateFailed,
        SwapchainCreateFailed,
        RendererCreateFailed,
    };

    class Application {
    public:
        using TickFn = std::function<void(Application&, const FrameContext&)>;

        [[nodiscard]]
        static std::expected<Application, ApplicationError> create(ApplicationConfig config = {});

        std::int16_t run(TickFn tick = {});
        void request_exit() noexcept;

        [[nodiscard]] strata::platform::Window& window() noexcept;
        [[nodiscard]] const strata::platform::Window& window() const noexcept;

        [[nodiscard]] strata::gfx::rhi::IGpuDevice& device() noexcept;
        [[nodiscard]] const strata::gfx::rhi::IGpuDevice& device() const noexcept;

        [[nodiscard]] strata::gfx::rhi::SwapchainHandle swapchain() const noexcept;

        [[nodiscard]] strata::gfx::renderer::Render2D& renderer() noexcept;
        [[nodiscard]] const strata::gfx::renderer::Render2D& renderer() const noexcept;

        [[nodiscard]] const ApplicationConfig& config() const noexcept;

    private:
        struct Impl;

        struct ImplDeleter {
            void operator()(Impl*) const noexcept;
        };

        explicit Application(std::unique_ptr<Impl, ImplDeleter> impl) noexcept
            : impl_(std::move(impl)) {}

        std::unique_ptr<Impl, ImplDeleter> impl_{ nullptr };
    };

} // namespace strata::core
