// path: engine/gfx/include/strata/gfx/renderer/basic_pass.h
// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/basic_pass.h
//
// Purpose:
//   Declare the BasicPass renderer.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "strata/gfx/renderer/camera_3d.h"
#include "strata/gfx/rhi/gpu_device.h"

namespace strata::base
{
class Diagnostics;
}

namespace strata::gfx::renderer
{

class RenderScene; // forward declaration (pass consumes scene data)

enum class BasicPassError : std::uint8_t
{
    InvalidSwapchain,
    CreateDescriptorSetLayoutFailed,
    CreatePipelineFailed,
    CreateDemoCubeVertexBufferFailed,
};

[[nodiscard]]
constexpr std::string_view to_string(BasicPassError e) noexcept
{
    switch (e)
    {
    case BasicPassError::InvalidSwapchain:
        return "InvalidSwapchain";
    case BasicPassError::CreateDescriptorSetLayoutFailed:
        return "CreateDescriptorSetLayoutFailed";
    case BasicPassError::CreatePipelineFailed:
        return "CreatePipelineFailed";
    case BasicPassError::CreateDemoCubeVertexBufferFailed:
        return "CreateDemoCubeVertexBufferFailed";
    }
    return "Unknown";
}

// BasicPass:
//   MVP forward pass that owns a pipeline + swapchain-sized resources and can
//   render either:
//     - the current demo (animated cube), OR
//     - a mesh supplied via RenderScene::world_mesh() / selected_mesh()
class BasicPass
{
  public:
    [[nodiscard]]
    static std::expected<BasicPass, BasicPassError> create(base::Diagnostics&   diagnostics,
                                                           rhi::IGpuDevice&     device,
                                                           rhi::SwapchainHandle swapchain);

    ~BasicPass();

    BasicPass(BasicPass&&) noexcept;
    BasicPass& operator=(BasicPass&&) noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    // Legacy camera API (kept so existing callers don't break).
    // New code should prefer: draw_frame(RenderScene const&).
    void                          set_camera(Camera3D const& camera) noexcept;
    [[nodiscard]] Camera3D const& camera() const noexcept;

    // New: consume RenderScene (RenderGraph/Renderer style).
    rhi::FrameResult draw_frame(RenderScene const& scene);

    // Legacy: draw using the internally stored camera (set_camera()).
    rhi::FrameResult draw_frame();

    rhi::FrameResult recreate_pipeline();

    // Called once the device is idle and we are about to recreate the swapchain.
    // Release resources that are sized or counted by the swapchain (depth images, per-image UBOs).
    // This keeps memory stable during resize storms and avoids retaining stale per-image resources.
    void on_before_swapchain_resize() noexcept;

  private:
    BasicPass() = default; // only create() can build a valid instance

    void release() noexcept;

    void             destroy_depth_textures() noexcept;
    rhi::FrameResult ensure_depth_texture(std::uint32_t image_index, rhi::Extent2D extent);

    void             destroy_ubo_resources() noexcept;
    rhi::FrameResult ensure_ubo_resources(std::uint32_t image_index);

    base::Diagnostics* diagnostics_{nullptr}; // non-owning
    rhi::IGpuDevice*   device_{nullptr};      // non-owning

    rhi::SwapchainHandle swapchain_{};
    rhi::PipelineHandle  pipeline_{};

    // Set 0: scene UBO (matrices + tint)
    rhi::DescriptorSetLayoutHandle ubo_layout_{};

    // IMPORTANT:
    // We support up to 2 draws per frame in BasicPass (base + selected).
    // Uniform buffers are not snapshotted at record time, so we must store
    // per-draw UBO data in distinct memory regions and bind distinct sets.
    static constexpr std::size_t ubo_slots_per_image = 2;

    // Per-swapchain-image descriptor sets (one per UBO slot)
    std::vector<std::array<rhi::DescriptorSetHandle, ubo_slots_per_image>> ubo_sets_{};

    // Per-swapchain-image UBO buffers (one buffer per UBO slot).
    //
    // Rationale:
    //   Vulkan requires VkDescriptorBufferInfo.offset for UNIFORM_BUFFER descriptors
    //   to be a multiple of minUniformBufferOffsetAlignment. By using one buffer per
    //   slot, we always bind offset=0 (valid on all devices) and avoid per-device
    //   padding/stride bookkeeping in the renderer layer.
    std::vector<std::array<rhi::BufferHandle, ubo_slots_per_image>> ubo_buffers_{};

    // Depth attachment (renderer-owned)
    rhi::Format                     depth_format_{rhi::Format::D24_UNorm_S8_UInt};
    rhi::Extent2D                   depth_extent_{};
    std::vector<rhi::TextureHandle> depth_textures_{};

    // Demo cube geometry (fallback when no world mesh is provided).
    // Vertex layout: float3 position at binding 0 / location 0.
    rhi::BufferHandle demo_cube_vb_{};
    std::uint32_t     demo_cube_vertex_count_{0};

    // Minimal 3D camera + simple animation
    Camera3D      camera_{};
    std::uint64_t frame_counter_{0};
};

} // namespace strata::gfx::renderer
