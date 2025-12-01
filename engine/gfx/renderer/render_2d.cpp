// -----------------------------------------------------------------------------
// engine/gfx/renderer/render_2d.cpp
//
// Purpose:
//   Implements the Render2D frontend on top of the RHI IGpuDevice interface.
//   Responsible for owning a basic graphics pipeline and cooperating with the
//   device for swapchain recreation on resize.
// -----------------------------------------------------------------------------

#include "render_2d.h"

#include <utility> // for std::move if we ever need it

namespace strata::gfx::renderer {

    using namespace strata::gfx::rhi;

    // -------------------------------------------------------------------------
    // Render2D
    // -------------------------------------------------------------------------

    Render2D::Render2D(IGpuDevice& device, SwapchainHandle swapchain)
        : device_(&device)
        , swapchain_(swapchain)
        , pipeline_{} {

        // Simple pipeline description: fullscreen triangle, flat color.
        // We can evolve this later into a proper material/shader system.
        PipelineDesc desc{};
        desc.vertex_shader_path = "shaders/fullscreen_triangle.vert.spv";
        desc.fragment_shader_path = "shaders/flat_color.frag.spv";
        desc.alpha_blend = false;

        pipeline_ = device_->create_pipeline(desc);
    }

    Render2D::~Render2D() {
        if (device_ && pipeline_) {
            device_->destroy_pipeline(pipeline_);
            pipeline_ = {};
        }
        device_ = nullptr;
        swapchain_ = {};
    }

    Render2D::Render2D(Render2D&& other) noexcept
        : device_(other.device_)
        , swapchain_(other.swapchain_)
        , pipeline_(other.pipeline_) {

        other.device_ = nullptr;
        other.swapchain_ = {};
        other.pipeline_ = {};
    }

    Render2D& Render2D::operator=(Render2D&& other) noexcept {
        if (this != &other) {
            // Release current pipeline if we own one.
            if (device_ && pipeline_) {
                device_->destroy_pipeline(pipeline_);
            }

            device_ = other.device_;
            swapchain_ = other.swapchain_;
            pipeline_ = other.pipeline_;

            other.device_ = nullptr;
            other.swapchain_ = {};
            other.pipeline_ = {};
        }
        return *this;
    }

    FrameResult Render2D::draw_frame() {
        if (!device_ || !swapchain_ || !pipeline_) {
            return FrameResult::Error;
        }

        // For now the device owns all low-level command recording.
        // This is essentially the same contract as our current GraphicsDevice::
        // draw_frame(swapchain, pipeline).
        return device_->present(swapchain_);
    }

    // -------------------------------------------------------------------------
    // Helper: draw_frame_and_handle_resize
    // -------------------------------------------------------------------------

    FrameResult draw_frame_and_handle_resize(
        IGpuDevice& device,
        SwapchainHandle& swapchain,
        Render2D& renderer,
        Extent2D framebuffer_size) {

        // Minimized / zero-area window: skip rendering but don't treat as error.
        if (framebuffer_size.width == 0 || framebuffer_size.height == 0) {
            return FrameResult::Ok;
        }

        FrameResult result = renderer.draw_frame();
        if (result == FrameResult::Ok) {
            return FrameResult::Ok;
        }
        if (result == FrameResult::Error) {
            return FrameResult::Error;
        }

        // Any non-Ok, non-Error result is treated as "swapchain needs resize".
        device.wait_idle();

        SwapchainDesc sc_desc{};
        sc_desc.size = framebuffer_size;
        sc_desc.vsync = true; // or expose as parameter later

        // Resize existing swapchain in-place.
        FrameResult resize_result = device.resize_swapchain(swapchain, sc_desc);
        if (resize_result == FrameResult::Error) {
            // Failed to resize; treat as non-fatal (no frame rendered).
            return FrameResult::Ok;
        }

        // Rebuild the pipeline via a fresh Render2D for the resized swapchain.
        renderer = Render2D{ device, swapchain };

        return FrameResult::Ok;
    }

} // namespace strata::gfx::renderer
