// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/render_graph.h
//
// Purpose:
//   Declare the RenderGraph renderer interface.
// -----------------------------------------------------------------------------

#pragma once
#include "strata/gfx/rhi/gpu_device.h"

namespace strata::gfx::renderer
{

class RenderGraph
{
  public:
    explicit RenderGraph(rhi::IGpuDevice& device);
    // TODO: declarative passes, resources, dependencies
};

} // namespace strata::gfx::renderer
