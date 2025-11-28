#include "gfx/renderer/render_scene.h"

namespace strata::gfx::renderer {

RenderScene::RenderScene(rhi::IGpuDevice& device)
    : device_{ &device } {}

} // namespace strata::gfx::renderer
