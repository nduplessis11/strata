#include "gfx/renderer/render_2d.h"

namespace strata::gfx::renderer {

Render2D::Render2D(rhi::IGpuDevice& device, rhi::SwapchainHandle swapchain)
    : device_{ &device }
    , swapchain_{ swapchain }
{
    pipeline_ = device_->create_pipeline({});
}

Render2D::~Render2D() {
    if (device_ && pipeline_) {
        device_->destroy_pipeline(pipeline_);
    }
}

Render2D::Render2D(Render2D&& other) noexcept = default;
Render2D& Render2D::operator=(Render2D&& other) noexcept = default;

rhi::FrameResult Render2D::draw_frame() {
    auto cmd = device_->begin_commands();
    device_->end_commands(cmd);
    device_->submit({ cmd });
    return device_->present(swapchain_);
}

rhi::FrameResult draw_frame_and_handle_resize(
    rhi::IGpuDevice& device,
    rhi::SwapchainHandle& swapchain,
    Render2D& renderer,
    rhi::Extent2D framebuffer_size)
{
    auto result = renderer.draw_frame();
    if (result == rhi::FrameResult::ResizeNeeded) {
        rhi::SwapchainDesc desc{};
        desc.size = framebuffer_size;
        auto resize_result = device.resize_swapchain(swapchain, desc);
        if (resize_result == rhi::FrameResult::Error) {
            return resize_result;
        }
        return rhi::FrameResult::Ok;
    }
    return result;
}

} // namespace strata::gfx::renderer
