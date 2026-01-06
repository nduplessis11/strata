// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/render_graph.h
//
// Purpose:
//   RenderGraph is the frame-driving "how to draw" layer. In MVP v1, it is a
//   thin wrapper around the existing Render2D implementation so we can evolve
//   toward a real pass/resource graph without a huge refactor.
// -----------------------------------------------------------------------------

#pragma once

#include <expected>

#include "strata/gfx/renderer/render_2d.h" // existing implementation
#include "strata/gfx/renderer/render_scene.h"
#include "strata/gfx/rhi/gpu_device.h"

namespace strata::base
{
class Diagnostics;
}

namespace strata::gfx::renderer
{

// For now, reuse the existing error type from Render2D. We can rename later.
using RenderGraphError = Render2DError;

class RenderGraph
{
  public:
    [[nodiscard]]
    static std::expected<RenderGraph, RenderGraphError> create(base::Diagnostics&   diagnostics,
                                                               rhi::IGpuDevice&     device,
                                                               rhi::SwapchainHandle swapchain);

    RenderGraph(RenderGraph&&) noexcept            = default;
    RenderGraph& operator=(RenderGraph&&) noexcept = default;

    RenderGraph(RenderGraph const&)            = delete;
    RenderGraph& operator=(RenderGraph const&) = delete;

    ~RenderGraph() = default;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return render_.is_valid();
    }

    rhi::FrameResult draw_frame(RenderScene const& scene);

    rhi::FrameResult recreate_pipeline()
    {
        return render_.recreate_pipeline();
    }

    void on_before_swapchain_resize() noexcept
    {
        render_.on_before_swapchain_resize();
    }

  private:
    explicit RenderGraph(Render2D&& r) noexcept
          : render_(std::move(r))
    {
    }

    Render2D render_;
};

} // namespace strata::gfx::renderer
