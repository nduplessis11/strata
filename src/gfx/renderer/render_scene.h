#pragma once

#include "gfx/rhi/gpu_device.h"

namespace strata::gfx::renderer {

class RenderScene {
public:
    explicit RenderScene(rhi::IGpuDevice& device);

private:
    rhi::IGpuDevice* device_{ nullptr };
};

} // namespace strata::gfx::renderer
