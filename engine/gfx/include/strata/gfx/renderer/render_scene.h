// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/render_scene.h
//
// Purpose:
//   Declare the RenderScene renderer interface.
// -----------------------------------------------------------------------------

#pragma once
#include "strata/gfx/rhi/gpu_device.h"

namespace strata::gfx::renderer {

    class RenderScene {
    public:
        explicit RenderScene(rhi::IGpuDevice& device);
        // TODO: methods for adding meshes, lights, camera, etc.
    };

} // namespace strata::gfx::renderer
