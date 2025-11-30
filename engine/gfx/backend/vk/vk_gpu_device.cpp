// engine/gfx/backend/vk/vk_gpu_device.cpp

#include "gfx/rhi/gpu_device.h"
#include "vk_gpu_device.h"

namespace strata::gfx::rhi {

    // RHI factory: chooses backend (currently only Vulkan) and forwards to VkGpuDevice.
    std::unique_ptr<IGpuDevice> create_device(
        const DeviceCreateInfo& info,
        const strata::platform::WsiHandle& surface)
    {
        switch (info.backend) {
        case BackendType::Vulkan:
        default:
            return vk::VkGpuDevice::create(info, surface);
        }
    }

} // namespace strata::gfx::rhi


// -----------------------------------------------------------------------------
// Vulkan backend implementation of IGpuDevice
// -----------------------------------------------------------------------------

namespace strata::gfx::vk {

    std::unique_ptr<VkGpuDevice> VkGpuDevice::create(
        const rhi::DeviceCreateInfo& info,
        const strata::platform::WsiHandle& surface)
    {
        auto device = std::unique_ptr<VkGpuDevice>(new VkGpuDevice());

        // For now, these are stubbed. You can later use real Vulkan code
        // to create instance/surface/device/command pools here.
        (void)info;
        device->instance_.init(surface);
        device->device_.init(device->instance_.instance());
        device->command_pool_.init(device->device_.device());

        return device;
    }

    VkGpuDevice::~VkGpuDevice() {
        // Make sure all GPU work is done before tearing down resources.
        wait_idle();
        command_pool_.cleanup(device_.device());
        swapchain_.cleanup(device_.device());
        device_.cleanup();
    }

    // --- Swapchain -----------------------------------------------------------

    rhi::SwapchainHandle VkGpuDevice::create_swapchain(
        const rhi::SwapchainDesc& desc,
        const strata::platform::WsiHandle&)
    {
        swapchain_.init(device_.device(), desc);
        // Stub: always return handle {1}
        return rhi::SwapchainHandle{ 1 };
    }

    rhi::FrameResult VkGpuDevice::present(rhi::SwapchainHandle) {
        // Stub: pretend present succeeded
        return rhi::FrameResult::Ok;
    }

    rhi::FrameResult VkGpuDevice::resize_swapchain(
        rhi::SwapchainHandle,
        const rhi::SwapchainDesc& desc)
    {
        swapchain_.cleanup(device_.device());
        swapchain_.init(device_.device(), desc);
        return rhi::FrameResult::Ok;
    }

    // --- Handle allocation helpers -------------------------------------------

    rhi::BufferHandle        VkGpuDevice::allocate_buffer_handle() { return rhi::BufferHandle{ next_buffer_++ }; }
    rhi::TextureHandle       VkGpuDevice::allocate_texture_handle() { return rhi::TextureHandle{ next_texture_++ }; }
    rhi::PipelineHandle      VkGpuDevice::allocate_pipeline_handle() { return rhi::PipelineHandle{ next_pipeline_++ }; }
    rhi::CommandBufferHandle VkGpuDevice::allocate_command_handle() { return rhi::CommandBufferHandle{ next_command_++ }; }

    // --- Buffers -------------------------------------------------------------

    rhi::BufferHandle VkGpuDevice::create_buffer(
        const rhi::BufferDesc&,
        std::span<const std::byte>)
    {
        // Stub: just hand out a unique handle
        return allocate_buffer_handle();
    }

    void VkGpuDevice::destroy_buffer(rhi::BufferHandle) {
        // Stub: nothing to do yet
    }

    // --- Textures ------------------------------------------------------------

    rhi::TextureHandle VkGpuDevice::create_texture(const rhi::TextureDesc&) {
        return allocate_texture_handle();
    }

    void VkGpuDevice::destroy_texture(rhi::TextureHandle) {
        // Stub
    }

    // --- Pipelines -----------------------------------------------------------

    rhi::PipelineHandle VkGpuDevice::create_pipeline(const rhi::PipelineDesc&) {
        return allocate_pipeline_handle();
    }

    void VkGpuDevice::destroy_pipeline(rhi::PipelineHandle) {
        // Stub
    }

    // --- Commands & submission ----------------------------------------------

    rhi::CommandBufferHandle VkGpuDevice::begin_commands() {
        // Stub: just allocate a logical command handle
        return allocate_command_handle();
    }

    void VkGpuDevice::end_commands(rhi::CommandBufferHandle) {
        // Stub
    }

    void VkGpuDevice::submit(const rhi::SubmitDesc&) {
        // Stub
    }

    // --- Synchronization -----------------------------------------------------

    void VkGpuDevice::wait_idle() {
        // Stub: in a real implementation, you'd call vkDeviceWaitIdle(device_.device())
    }

} // namespace strata::gfx::vk
