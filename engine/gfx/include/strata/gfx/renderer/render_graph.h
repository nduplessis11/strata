// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/render_graph.h
//
// Purpose:
//   RenderGraph is the frame-driving "how to draw" layer. In MVP v1, it is a
//   thin wrapper around a single basic forward pass so we can evolve toward a
//   real pass/resource graph without a huge refactor.
// -----------------------------------------------------------------------------

#pragma once

#include <expected>

#include "strata/gfx/renderer/basic_pass.h"
#include "strata/gfx/renderer/render_scene.h"
#include "strata/gfx/rhi/gpu_device.h"

namespace strata::base
{
class Diagnostics;
}

namespace strata::gfx::renderer
{

// For now, reuse the error type from the basic pass.
using RenderGraphError = BasicPassError;

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
        return pass_.is_valid();
    }

    rhi::FrameResult draw_frame(RenderScene const& scene);

    rhi::FrameResult recreate_pipeline()
    {
        return pass_.recreate_pipeline();
    }

    void on_before_swapchain_resize() noexcept
    {
        pass_.on_before_swapchain_resize();
    }

  private:
    explicit RenderGraph(BasicPass&& pass) noexcept
          : pass_(std::move(pass))
    {
    }

    BasicPass pass_;
};

} // namespace strata::gfx::renderer
