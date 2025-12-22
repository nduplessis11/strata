// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/render_2d.h
//
// Purpose:
//   Declare the Render2D renderer and helper draw function.
// -----------------------------------------------------------------------------

#pragma once

#include "strata/gfx/rhi/gpu_device.h"

namespace strata::gfx::renderer
{

class Render2D
{
  public:
    Render2D(rhi::IGpuDevice&     device,
             rhi::SwapchainHandle swapchain);
    ~Render2D();

    Render2D(Render2D&&) noexcept;
    Render2D& operator=(Render2D&&) noexcept;

    rhi::FrameResult draw_frame();

  private:
    rhi::IGpuDevice*     device_{nullptr}; // non-owning
    rhi::SwapchainHandle swapchain_{};
    rhi::PipelineHandle  pipeline_{};
};

rhi::FrameResult draw_frame_and_handle_resize(rhi::IGpuDevice&      device,
                                              rhi::SwapchainHandle& swapchain,
                                              Render2D&             renderer,
                                              rhi::Extent2D         framebuffer_size);

} // namespace strata::gfx::renderer
