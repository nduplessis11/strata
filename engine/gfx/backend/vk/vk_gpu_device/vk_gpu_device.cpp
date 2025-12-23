// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device.cpp
//
// Purpose:
//   Implement the Vulkan IGpuDevice backend core lifecycle.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

namespace strata::gfx::vk
{

std::unique_ptr<VkGpuDevice> VkGpuDevice::create(rhi::DeviceCreateInfo const&       info,
                                                 strata::platform::WsiHandle const& surface)
{
    (void)info; // later: debug flags, frames-in-flight, etc.

    auto dev = std::unique_ptr<VkGpuDevice>(new VkGpuDevice());

    // 1) Instance + surface
    if (!dev->instance_.init(surface))
    {
        return {};
    }

    // 2) Physical + logical device + queues
    if (!dev->device_.init(dev->instance_.instance(), dev->instance_.surface()))
    {
        return {};
    }

    // 3) Command pool
    if (!dev->command_pool_.init(dev->device_.device(), dev->device_.graphics_family()))
    {
        return {};
    }

    // 4) Frames-in-flight ring
    dev->frames_in_flight_ = 2; // start with 2
    if (!dev->init_frames())
    {
        return {};
    }

    // Basic pipeline is created after the first swapchain is created, because
    // it needs the swapchain color format. For now, we defer it to create_pipeline.

    return dev;
}

VkGpuDevice::~VkGpuDevice()
{
    // Be nice and let the GPU finish.
    wait_idle();

    // Destroy pipeline first (it holds the device handle)
    basic_pipeline_ = BasicPipeline{};

    // Destroy sync primitives
    destroy_render_finished_per_image();
    destroy_frames();

    // Command pool
    command_pool_.cleanup(device_.device());

    // Swapchain images + views + swapchain
    swapchain_.cleanup();
    swapchain_image_layouts_.clear();

    // Logical device
    device_.cleanup();
    // Instance + surface cleaned up by VkInstanceWrapper destructor
}

// --- Handle allocation helpers -------------------------------------------

rhi::BufferHandle VkGpuDevice::allocate_buffer_handle()
{
    return rhi::BufferHandle{next_buffer_++};
}

rhi::TextureHandle VkGpuDevice::allocate_texture_handle()
{
    return rhi::TextureHandle{next_texture_++};
}

rhi::PipelineHandle VkGpuDevice::allocate_pipeline_handle()
{
    return rhi::PipelineHandle{next_pipeline_++};
}

rhi::CommandBufferHandle VkGpuDevice::allocate_command_handle()
{
    return rhi::CommandBufferHandle{next_command_++};
}

// --- Synchronization -----------------------------------------------------

void VkGpuDevice::wait_idle()
{
    if (device_.device() != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_.device());
    }
}

} // namespace strata::gfx::vk
