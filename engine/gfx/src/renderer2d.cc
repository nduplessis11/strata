// -----------------------------------------------------------------------------
// engine/gfx/src/renderer2d.cc
//
// Purpose:
//   Implements the Renderer2d frontend, wiring frame submission to the active
//   GraphicsDevice and handling swapchain recreation on resize.
// -----------------------------------------------------------------------------

#include "strata/gfx/renderer2d.h"

#include <utility>

namespace strata::gfx {

    Renderer2d::Renderer2d(GraphicsDevice& device, GraphicsSwapchain& swapchain)
        : device_(&device)
        , swapchain_(&swapchain) {
        pipeline_ = device.create_pipeline(swapchain);
    }

    Renderer2d::~Renderer2d() = default;
    Renderer2d::Renderer2d(Renderer2d&&) noexcept = default;
    Renderer2d& Renderer2d::operator=(Renderer2d&&) noexcept = default;

    FrameResult Renderer2d::draw_frame() {
        if (!device_ || !swapchain_) {
            return FrameResult::Error;
        }
        return device_->draw_frame(*swapchain_, pipeline_.get());
    }

    FrameResult draw_frame_and_handle_resize(
        GraphicsDevice& device,
        std::unique_ptr<GraphicsSwapchain>& swapchain,
        Renderer2d& renderer,
        strata::platform::Extent2d framebuffer_size) {

        if (framebuffer_size.width == 0 || framebuffer_size.height == 0) {
            return FrameResult::Ok;
        }

        FrameResult result{ renderer.draw_frame() };
        if (result == FrameResult::Ok) {
            return FrameResult::Ok;
        }
        if (result == FrameResult::Error) {
            return FrameResult::Error;
        }

        device.wait_idle();

        auto new_swapchain = device.create_swapchain(framebuffer_size, swapchain.get());
        if (!new_swapchain || !new_swapchain->is_valid()) {
            return FrameResult::Ok;
        }

        swapchain = std::move(new_swapchain);
        renderer = Renderer2d{ device, *swapchain };
        return FrameResult::Ok;
    }

} // namespace strata::gfx
