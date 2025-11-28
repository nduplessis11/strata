#include "gfx/renderer/render_graph.h"

namespace strata::gfx::renderer {

RenderGraph::RenderGraph(rhi::IGpuDevice& device)
    : device_{ &device } {}

} // namespace strata::gfx::renderer
