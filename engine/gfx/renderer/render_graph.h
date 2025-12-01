// engine/gfx/renderer/render_graph.h
#pragma once
#include "gfx/rhi/gpu_device.h"

namespace strata::gfx::renderer {

    class RenderGraph {
    public:
        explicit RenderGraph(rhi::IGpuDevice& device);
        // TODO: declarative passes, resources, dependencies
    };

} // namespace strata::gfx::renderer
