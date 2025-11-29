// engine/gfx/backend/vk/vk_gpu_device.h
#pragma once

#include <memory>
#include "gfx/rhi/gpu_device.h"
#include "vk_instance.h"
#include "vk_device.h"
#include "vk_swapchain.h"
#include "vk_command_buffer.h"

namespace strata::gfx::vk {

    class VkGpuDevice final : public rhi::IGpuDevice {
    public:
        static std::unique_ptr<VkGpuDevice> create(
            const rhi::DeviceCreateInfo& info,
            const strata::platform::WsiHandle& surface);

        ~VkGpuDevice() override;

        // IGpuDevice implementation...
        rhi::SwapchainHandle create_swapchain(const rhi::SwapchainDesc&,
            const strata::platform::WsiHandle&) override;
        rhi::FrameResult      present(rhi::SwapchainHandle) override;
        rhi::FrameResult      resize_swapchain(rhi::SwapchainHandle,
            const rhi::SwapchainDesc&) override;

        rhi::BufferHandle     create_buffer(const rhi::BufferDesc&,
            std::span<const std::byte>) override;
        void                  destroy_buffer(rhi::BufferHandle) override;

        // ... textures, pipelines, commands, submit, wait_idle

    private:
        VkInstanceWrapper instance_;
        VkDeviceWrapper   device_;
        // some arrays/vectors mapping rhi::BufferHandle to VkBuffer, etc.
    };

} // namespace strata::gfx::vk
