// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer2d.h
//
// Purpose:
//   High-level 2D renderer front-end. Provides a small, API-agnostic surface
//   over the graphics backend. All API-specific work is handled by the backend
//   implementation of GraphicsDevice.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>

#include "strata/gfx/graphics_device.h"

namespace strata::gfx {

    class Renderer2d {
    public:
        Renderer2d(GraphicsDevice& device, GraphicsSwapchain& swapchain);
        ~Renderer2d();

        Renderer2d(Renderer2d&&) noexcept;
        Renderer2d& operator=(Renderer2d&&) noexcept;

        Renderer2d(const Renderer2d&) = delete;
        Renderer2d& operator=(const Renderer2d&) = delete;

        [[nodiscard]] FrameResult draw_frame();

    private:
        GraphicsDevice* device_{ nullptr };    // non-owning
        GraphicsSwapchain* swapchain_{ nullptr }; // non-owning
        std::unique_ptr<GraphicsPipeline> pipeline_{};
    };

    [[nodiscard]] FrameResult draw_frame_and_handle_resize(
        GraphicsDevice& device,
        std::unique_ptr<GraphicsSwapchain>& swapchain,
        Renderer2d& renderer,
        strata::platform::Extent2d framebuffer_size);

} // namespace strata::gfx
