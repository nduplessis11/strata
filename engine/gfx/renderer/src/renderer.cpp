// -----------------------------------------------------------------------------
// engine/gfx/renderer/src/renderer.cpp
// -----------------------------------------------------------------------------

#include "strata/gfx/renderer/renderer.h"

#include "strata/base/diagnostics.h"

namespace strata::gfx::renderer
{

std::expected<Renderer, RendererError> Renderer::create(base::Diagnostics&   diagnostics,
                                                        rhi::IGpuDevice&     device,
                                                        rhi::SwapchainHandle swapchain)
{
    auto graph_exp = RenderGraph::create(diagnostics, device, swapchain);
    if (!graph_exp)
        return std::unexpected(graph_exp.error());

    return Renderer{std::move(*graph_exp)};
}

} // namespace strata::gfx::renderer
