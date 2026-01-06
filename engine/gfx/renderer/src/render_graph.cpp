// -----------------------------------------------------------------------------
// engine/gfx/renderer/src/render_graph.cpp
//
// Purpose:
//   MVP v1: RenderGraph is a thin wrapper around Render2D.
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
    auto exp = Render2D::create(diagnostics, device, swapchain);
    if (!exp)
        return std::unexpected(exp.error());

    return RenderGraph{std::move(*exp)};
}

rhi::FrameResult RenderGraph::draw_frame(RenderScene const& scene)
{
    // MVP v1: Render2D owns the per-frame rendering logic. We feed it the camera.
    render_.set_camera(scene.camera());

    // NOTE: world_mesh/selected_mesh are not yet used by Render2D. Those will be
    // wired once the mesh-capable pipeline lands.
    return render_.draw_frame();
}

} // namespace strata::gfx::renderer
