#pragma once

#include <memory>

#include "gfx/rhi/gpu_device.h"
#include "gfx/backend/vk/vk_command_buffer.h"
#include "gfx/backend/vk/vk_device.h"
#include "gfx/backend/vk/vk_instance.h"
#include "gfx/backend/vk/vk_swapchain.h"

namespace strata::gfx::vk {

class VkGpuDevice final : public rhi::IGpuDevice {
public:
    static std::unique_ptr<VkGpuDevice> create(
        const rhi::DeviceCreateInfo& info,
        const strata::platform::WsiHandle& surface);

    ~VkGpuDevice() override;

    rhi::SwapchainHandle create_swapchain(const rhi::SwapchainDesc& desc,
                                          const strata::platform::WsiHandle& surface) override;
    rhi::FrameResult      present(rhi::SwapchainHandle swapchain) override;
    rhi::FrameResult      resize_swapchain(rhi::SwapchainHandle swapchain,
                                           const rhi::SwapchainDesc& desc) override;

    rhi::BufferHandle     create_buffer(const rhi::BufferDesc& desc,
                                        std::span<const std::byte> initial_data) override;
    void                  destroy_buffer(rhi::BufferHandle handle) override;

    rhi::TextureHandle create_texture(const rhi::TextureDesc& desc) override;
    void              destroy_texture(rhi::TextureHandle handle) override;

    rhi::PipelineHandle create_pipeline(const rhi::PipelineDesc& desc) override;
    void                destroy_pipeline(rhi::PipelineHandle handle) override;

    rhi::CommandBufferHandle begin_commands() override;
    void                     end_commands(rhi::CommandBufferHandle cmd) override;
    void                     submit(const rhi::SubmitDesc& submit) override;

    void wait_idle() override;

private:
    VkGpuDevice() = default;

    rhi::BufferHandle   allocate_handle();
    rhi::TextureHandle  allocate_texture_handle();
    rhi::PipelineHandle allocate_pipeline_handle();
    rhi::CommandBufferHandle allocate_command_handle();

    VkInstanceWrapper    instance_{};
    VkDeviceWrapper      device_{};
    VkSwapchainWrapper   swapchain_{};
    VkCommandBufferPool  command_pool_{};

    std::uint32_t next_buffer_{ 1 };
    std::uint32_t next_texture_{ 1 };
    std::uint32_t next_pipeline_{ 1 };
    std::uint32_t next_command_{ 1 };
};

} // namespace strata::gfx::vk
