// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/render_scene.h
//
// Purpose:
//   Scene state consumed by the renderer. This is a data container: it does not
//   own GPU resources or talk to Vulkan/RHI directly.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>

#include "strata/gfx/renderer/camera_3d.h"
#include "strata/gfx/rhi/gpu_types.h"

namespace strata::gfx::renderer
{

struct GpuMesh
{
    rhi::BufferHandle vertex_buffer{};
    rhi::BufferHandle index_buffer{};
    std::uint32_t     index_count{0};
    rhi::IndexType    index_type{rhi::IndexType::UInt32};
};

class RenderScene
{
  public:
    void set_camera(Camera3D const& c) noexcept
    {
        camera_ = c;
    }
    [[nodiscard]] Camera3D const& camera() const noexcept
    {
        return camera_;
    }

    void set_world_mesh(GpuMesh m) noexcept
    {
        world_ = m;
    }
    [[nodiscard]] GpuMesh const& world_mesh() const noexcept
    {
        return world_;
    }

    void set_selected_mesh(GpuMesh m) noexcept
    {
        selected_ = m;
    }
    void clear_selected_mesh() noexcept
    {
        selected_ = {};
    }
    [[nodiscard]] GpuMesh const& selected_mesh() const noexcept
    {
        return selected_;
    }

  private:
    Camera3D camera_{};
    GpuMesh  world_{};
    GpuMesh  selected_{};
};

} // namespace strata::gfx::renderer
