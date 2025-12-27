// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device.h
//
// Purpose:
//   Vulkan implementation of the RHI IGpuDevice interface.
//   This is backend-private (Vulkan types allowed here).
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "strata/gfx/rhi/gpu_device.h"
#include "strata/gfx/rhi/gpu_types.h"

// Backend-only wrapper headers (relative to vk_gpu_device/ folder)
#include "../vk_command_buffer.h"
#include "../vk_descriptor.h"
#include "../vk_device.h"
#include "../vk_instance.h"
#include "../vk_pipeline_basic.h"
#include "../vk_swapchain.h"

namespace strata::base
{
class Diagnostics;
}

namespace strata::gfx::vk
{

class VkGpuDevice final : public rhi::IGpuDevice
{
  public:
    ~VkGpuDevice() override;

    // Factory (backend uses explicit creation; VkGpuDevice ctor is private)
    static std::unique_ptr<VkGpuDevice> create(base::Diagnostics&           diagnostics,
                                               rhi::DeviceCreateInfo const& info,
                                               platform::WsiHandle const&   surface);

    // --- Swapchain -----------------------------------------------------------
    rhi::SwapchainHandle create_swapchain(rhi::SwapchainDesc const&  desc,
                                          platform::WsiHandle const& surface) override;
    rhi::FrameResult     resize_swapchain(rhi::SwapchainHandle      swapchain,
                                          rhi::SwapchainDesc const& desc) override;
    rhi::FrameResult     acquire_next_image(rhi::SwapchainHandle swapchain,
                                            rhi::AcquiredImage&  out) override;
    rhi::FrameResult present(rhi::SwapchainHandle swapchain, std::uint32_t image_index) override;

    // --- Buffers -------------------------------------------------------------
    rhi::BufferHandle create_buffer(rhi::BufferDesc const&     desc,
                                    std::span<std::byte const> initial_data = {}) override;
    void              destroy_buffer(rhi::BufferHandle handle) override;

    // --- Textures ------------------------------------------------------------
    rhi::TextureHandle create_texture(rhi::TextureDesc const& desc) override;
    void               destroy_texture(rhi::TextureHandle handle) override;

    // --- Pipelines -----------------------------------------------------------
    rhi::PipelineHandle create_pipeline(rhi::PipelineDesc const& desc) override;
    void                destroy_pipeline(rhi::PipelineHandle handle) override;

    // --- Commands & submission ----------------------------------------------
    rhi::CommandBufferHandle begin_commands() override;
    rhi::FrameResult         end_commands(rhi::CommandBufferHandle cmd) override;

    rhi::FrameResult submit(SubmitDesc const& submit) override;

    // --- Descriptor sets -----------------------------------------------------
    rhi::DescriptorSetLayoutHandle create_descriptor_set_layout(
        rhi::DescriptorSetLayoutDesc const& desc) override;
    void destroy_descriptor_set_layout(rhi::DescriptorSetLayoutHandle handle) override;

    rhi::DescriptorSetHandle allocate_descriptor_set(
        rhi::DescriptorSetLayoutHandle layout) override;
    void free_descriptor_set(rhi::DescriptorSetHandle set) override;

    rhi::FrameResult update_descriptor_set(rhi::DescriptorSetHandle              set,
                                           std::span<rhi::DescriptorWrite const> writes) override;

    // --- Recording (explicit functions fine for now) -------------------------
    rhi::FrameResult cmd_bind_descriptor_set(rhi::CommandBufferHandle cmd,
                                             rhi::PipelineHandle      pipeline,
                                             std::uint32_t            set_index,
                                             rhi::DescriptorSetHandle set) override;

    rhi::FrameResult cmd_begin_swapchain_pass(rhi::CommandBufferHandle cmd,
                                              rhi::SwapchainHandle     swapchain,
                                              std::uint32_t            image_index,
                                              rhi::ClearColor const&   clear) override;

    rhi::FrameResult cmd_end_swapchain_pass(rhi::CommandBufferHandle cmd,
                                            rhi::SwapchainHandle     swapchain,
                                            std::uint32_t            image_index) override;

    rhi::FrameResult cmd_bind_pipeline(rhi::CommandBufferHandle cmd,
                                       rhi::PipelineHandle      pipeline) override;

    rhi::FrameResult cmd_set_viewport_scissor(rhi::CommandBufferHandle cmd,
                                              rhi::Extent2D            extent) override;

    rhi::FrameResult cmd_draw(rhi::CommandBufferHandle cmd,
                              std::uint32_t            vertex_count,
                              std::uint32_t            instance_count,
                              std::uint32_t            first_vertex,
                              std::uint32_t            first_instance) override;

    void wait_idle() override;

  private:
    VkGpuDevice() = default;

    // --- Frame ring ----------------------------------------------------------
    struct FrameSlot
    {
        VkCommandBuffer cmd{VK_NULL_HANDLE};
        VkSemaphore     image_available{VK_NULL_HANDLE};
        VkFence         in_flight{VK_NULL_HANDLE};
    };

    struct SwapchainSync
    {
        std::vector<VkSemaphore> render_finished_per_image{};
    };

    // --- Buffer registry ------------------------------------------------------
    struct BufferRecord
    {
        VkBuffer       buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        std::uint64_t  size_bytes{0};
        void*          mapped{nullptr}; // only for host_visible buffers
        bool           host_visible{false};
    };

    bool init_frames();
    void destroy_frames();

    bool init_render_finished_per_image(std::size_t image_count);
    void destroy_render_finished_per_image();

    // --- Handle allocation (simple monotonic IDs) ----------------------------
    rhi::BufferHandle        allocate_buffer_handle();
    rhi::TextureHandle       allocate_texture_handle();
    rhi::PipelineHandle      allocate_pipeline_handle();
    rhi::CommandBufferHandle allocate_command_handle();

    // --- Descriptor internals ------------------------------------------------
    bool ensure_descriptor_pool();

    rhi::DescriptorSetLayoutHandle allocate_descriptor_set_layout_handle();
    rhi::DescriptorSetHandle       allocate_descriptor_set_handle();

    VkDescriptorSetLayout get_vk_descriptor_set_layout(
        rhi::DescriptorSetLayoutHandle handle) const noexcept;
    VkDescriptorSet get_vk_descriptor_set(rhi::DescriptorSetHandle handle) const noexcept;
    void            cleanup_descriptors();

    // --- Buffer internals ----------------------------------------------------
    VkBuffer get_vk_buffer(rhi::BufferHandle handle) const noexcept;
    void     cleanup_buffers();

    // --- Diagnostics (explicitly provided by Application) ---------------------
    base::Diagnostics* diagnostics_{nullptr}; // non-owning

    // --- Backend state -------------------------------------------------------
    VkInstanceWrapper                           instance_{};
    VkDeviceWrapper                             device_{};
    VkSwapchainWrapper                          swapchain_{};
    VkCommandBufferPool                         command_pool_{};
    BasicPipeline                               basic_pipeline_{};
    std::vector<rhi::DescriptorSetLayoutHandle> pipeline_set_layout_handles_{};

    // Frames in flight (ring)
    std::uint32_t          frames_in_flight_{2};
    std::uint32_t          frame_index_{0};
    std::vector<FrameSlot> frames_{};

    // Per-swapchain-image sync/state
    SwapchainSync              swapchain_sync_{};
    std::vector<VkFence>       images_in_flight_{};
    std::vector<VkImageLayout> swapchain_image_layouts_{};

    // Recording state (simple invariant checks)
    bool          recording_active_{false};
    std::uint32_t recording_frame_index_{0};

    // Resource handle counters
    std::uint32_t next_buffer_{1};
    std::uint32_t next_texture_{1};
    std::uint32_t next_pipeline_{1};
    std::uint32_t next_command_{1};

    // Descriptor handle counters + registries
    std::uint32_t                      next_descriptor_set_layout_{1};
    std::uint32_t                      next_descriptor_set_{1};
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts_{};
    std::vector<VkDescriptorSet>       descriptor_sets_{};

    std::vector<BufferRecord> buffers_{};

    // Single global pool (simple v1): lazily created.
    // Stored as optional because VkDescriptorPoolWrapper is move-only and not
    // default-constructible.
    std::optional<VkDescriptorPoolWrapper> descriptor_pool_{};
};

} // namespace strata::gfx::vk
