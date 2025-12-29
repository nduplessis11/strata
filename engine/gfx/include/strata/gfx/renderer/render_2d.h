// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/render_2d.h
//
// Purpose:
//   Declare the Render2D renderer and helper draw function.
// -----------------------------------------------------------------------------

#pragma once

#include <vector>

#include "strata/gfx/rhi/gpu_device.h"

namespace
{
class Diagnostics;
}

namespace strata::gfx::renderer
{

class Render2D
{
  public:
    Render2D(base::Diagnostics&   diagnostics,
             rhi::IGpuDevice&     device,
             rhi::SwapchainHandle swapchain);
    ~Render2D();

    Render2D(Render2D&&) noexcept;
    Render2D& operator=(Render2D&&) noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    rhi::FrameResult draw_frame();
    rhi::FrameResult recreate_pipeline();

  private:
    void release() noexcept;

    void             destroy_depth_textures() noexcept;
    rhi::FrameResult ensure_depth_texture(std::uint32_t image_index, rhi::Extent2D extent);

    base::Diagnostics* diagnostics_{nullptr}; // non-owning
    rhi::IGpuDevice*   device_{nullptr};      // non-owning

    rhi::SwapchainHandle swapchain_{};
    rhi::PipelineHandle  pipeline_{};

    rhi::DescriptorSetLayoutHandle ubo_layout_{};
    rhi::DescriptorSetHandle       ubo_set_{};
    rhi::BufferHandle              ubo_buffer_{};

    // Depth attachment (renderer-owned)
    rhi::Format                     depth_format_{rhi::Format::D24_UNorm_S8_UInt};
    rhi::Extent2D                   depth_extent_{};
    std::vector<rhi::TextureHandle> depth_textures_{};
};

rhi::FrameResult draw_frame_and_handle_resize(rhi::IGpuDevice&      device,
                                              rhi::SwapchainHandle& swapchain,
                                              Render2D&             renderer,
                                              rhi::Extent2D         framebuffer_size,
                                              base::Diagnostics&    diagnostics);

} // namespace strata::gfx::renderer
