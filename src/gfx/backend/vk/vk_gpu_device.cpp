#include "gfx/backend/vk/vk_gpu_device.h"

namespace strata::gfx::vk {

std::unique_ptr<VkGpuDevice> VkGpuDevice::create(
    const rhi::DeviceCreateInfo& info,
    const strata::platform::WsiHandle& surface)
{
    auto device = std::unique_ptr<VkGpuDevice>(new VkGpuDevice());
    (void)info;
    device->instance_.init(surface);
    device->device_.init(device->instance_.instance());
    device->command_pool_.init(device->device_.device());
    return device;
}

VkGpuDevice::~VkGpuDevice() {
    wait_idle();
    command_pool_.cleanup(device_.device());
    swapchain_.cleanup(device_.device());
    device_.cleanup();
}

rhi::SwapchainHandle VkGpuDevice::create_swapchain(const rhi::SwapchainDesc& desc,
                                                   const strata::platform::WsiHandle&) {
    swapchain_.init(device_.device(), desc);
    return rhi::SwapchainHandle{ 1 };
}

rhi::FrameResult VkGpuDevice::present(rhi::SwapchainHandle) {
    return rhi::FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::resize_swapchain(rhi::SwapchainHandle, const rhi::SwapchainDesc& desc) {
    swapchain_.cleanup(device_.device());
    swapchain_.init(device_.device(), desc);
    return rhi::FrameResult::Ok;
}

rhi::BufferHandle VkGpuDevice::allocate_handle() { return rhi::BufferHandle{ next_buffer_++ }; }
rhi::TextureHandle VkGpuDevice::allocate_texture_handle() { return rhi::TextureHandle{ next_texture_++ }; }
rhi::PipelineHandle VkGpuDevice::allocate_pipeline_handle() { return rhi::PipelineHandle{ next_pipeline_++ }; }
rhi::CommandBufferHandle VkGpuDevice::allocate_command_handle() { return rhi::CommandBufferHandle{ next_command_++ }; }

rhi::BufferHandle VkGpuDevice::create_buffer(const rhi::BufferDesc&, std::span<const std::byte>) {
    return allocate_handle();
}

void VkGpuDevice::destroy_buffer(rhi::BufferHandle) {}

rhi::TextureHandle VkGpuDevice::create_texture(const rhi::TextureDesc&) { return allocate_texture_handle(); }
void VkGpuDevice::destroy_texture(rhi::TextureHandle) {}

rhi::PipelineHandle VkGpuDevice::create_pipeline(const rhi::PipelineDesc&) { return allocate_pipeline_handle(); }
void VkGpuDevice::destroy_pipeline(rhi::PipelineHandle) {}

rhi::CommandBufferHandle VkGpuDevice::begin_commands() { return allocate_command_handle(); }
void VkGpuDevice::end_commands(rhi::CommandBufferHandle) {}
void VkGpuDevice::submit(const rhi::SubmitDesc&) {}

void VkGpuDevice::wait_idle() {}

} // namespace strata::gfx::vk
