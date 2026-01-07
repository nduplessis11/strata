// -----------------------------------------------------------------------------
// engine/gfx/renderer/src/render_graph.cpp
//
// Purpose:
//   MVP v1: RenderGraph is a thin wrapper around a single BasicPass.
// -----------------------------------------------------------------------------

#include <expected>

#include "strata/gfx/renderer/render_graph.h"

#include "strata/base/diagnostics.h"

namespace strata::gfx::renderer
{

std::expected<RenderGraph, RenderGraphError> RenderGraph::create(base::Diagnostics&   diagnostics,
                                                                 rhi::IGpuDevice&     device,
                                                                 rhi::SwapchainHandle swapchain)
{
    auto exp = BasicPass::create(diagnostics, device, swapchain);
    if (!exp)
        return std::unexpected(exp.error());

    return RenderGraph{std::move(*exp)};
}

rhi::FrameResult RenderGraph::draw_frame(RenderScene const& scene)
{
    // MVP v1: a single pass consumes the whole RenderScene.
    return pass_.draw_frame(scene);
}

} // namespace strata::gfx::renderer
