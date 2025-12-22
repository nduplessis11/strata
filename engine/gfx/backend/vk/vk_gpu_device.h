// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device.h
//
// Purpose:
//   Declare the Vulkan IGpuDevice implementation.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "strata/gfx/rhi/gpu_device.h"
#include "vk_command_buffer.h"
#include "vk_device.h"
#include "vk_instance.h"
#include "vk_pipeline_basic.h"
#include "vk_swapchain.h"

namespace strata::gfx::vk
{

class VkGpuDevice final : public rhi::IGpuDevice
{
  public:
    static std::unique_ptr<VkGpuDevice> create(rhi::DeviceCreateInfo const&       info,
                                               strata::platform::WsiHandle const& surface);

    ~VkGpuDevice() override;

    // --- Swapchain -------------------------------------------------------
    rhi::SwapchainHandle create_swapchain(rhi::SwapchainDesc const&          desc,
                                          strata::platform::WsiHandle const& surface) override;
    rhi::FrameResult     resize_swapchain(rhi::SwapchainHandle      swapchain,
                                          rhi::SwapchainDesc const& desc) override;
    rhi::FrameResult     present(rhi::SwapchainHandle swapchain,
                                 std::uint32_t        image_index) override;
    rhi::FrameResult     acquire_next_image(rhi::SwapchainHandle swapchain,
                                            rhi::AcquiredImage&  out) override;

    // --- Buffers ---------------------------------------------------------
    rhi::BufferHandle create_buffer(rhi::BufferDesc const&     desc,
                                    std::span<std::byte const> initial_data) override;
    void              destroy_buffer(rhi::BufferHandle handle) override;

    // --- Textures --------------------------------------------------------
    rhi::TextureHandle create_texture(rhi::TextureDesc const& desc) override;
    void               destroy_texture(rhi::TextureHandle handle) override;

    // --- Pipelines -------------------------------------------------------
    rhi::PipelineHandle create_pipeline(rhi::PipelineDesc const& desc) override;
    void                destroy_pipeline(rhi::PipelineHandle handle) override;

    // --- Commands & submission -------------------------------------------
    rhi::CommandBufferHandle begin_commands() override;
    rhi::FrameResult         end_commands(rhi::CommandBufferHandle cmd) override;
    rhi::FrameResult         submit(rhi::IGpuDevice::SubmitDesc const& submit) override;

    // --- Recording (explicit functions fine for now)
    // -------------------------------------------------------------
    // TODO: turn these into a CommandList/Encoder object later for nicer API

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

    // --- Synchronization -------------------------------------------------
    void wait_idle() override;

  private:
    // Per-frame synchronization + commad buffer (frames-in-flight ring)
    struct FrameSlot
    {
        VkCommandBuffer cmd{VK_NULL_HANDLE};
        VkSemaphore     image_available{VK_NULL_HANDLE};
        VkFence         in_flight{VK_NULL_HANDLE};
    };

    // Swapchain-dependent sync (still per swapchain-image for now)
    // Keeping this per-image lets present() stay: present(swapchain, image_index)
    struct SwapchainSync
    {
        std::vector<VkSemaphore> render_finished_per_image;
    };

  private:
    VkGpuDevice() = default;

    rhi::BufferHandle        allocate_buffer_handle();
    rhi::TextureHandle       allocate_texture_handle();
    rhi::PipelineHandle      allocate_pipeline_handle();
    rhi::CommandBufferHandle allocate_command_handle();

    VkInstanceWrapper   instance_{};
    VkDeviceWrapper     device_{};
    VkSwapchainWrapper  swapchain_{};
    VkCommandBufferPool command_pool_{};

    // Start with 2; later feed from DeviceCreateInfo
    std::uint32_t frames_in_flight_{2};
    std::uint32_t frame_index_{0};
    // Tracks which frame slot is currently being recorded/submitted.
    // This avoids bugs if frame_index_ advances while cmd_* still runs.
    std::uint32_t recording_frame_index_{0};
    bool          recording_active_{false};

    // Per-frame ring resources
    std::vector<FrameSlot> frames_;

    // Swapchain-dependent sync/state
    SwapchainSync swapchain_sync_{};

    // Fence tracking per swapchain image (prevents reusing an image still in flight)
    std::vector<VkFence> images_in_flight_;

    BasicPipeline basic_pipeline_{};

    std::vector<VkImageLayout> swapchain_image_layouts_;

    std::uint32_t next_buffer_{1};
    std::uint32_t next_texture_{1};
    std::uint32_t next_pipeline_{1};
    std::uint32_t next_command_{1};

  private:
    // --- helpers ---------------------------------------------------------------
    bool init_frames();
    void destroy_frames();

    bool init_render_finished_per_image(std::size_t image_count);
    void destroy_render_finished_per_image();
};

} // namespace strata::gfx::vk
