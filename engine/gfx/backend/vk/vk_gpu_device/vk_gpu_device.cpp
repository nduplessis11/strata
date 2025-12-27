// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device.cpp
//
// Purpose:
//   Implement the Vulkan IGpuDevice backend core lifecycle.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include "../vk_check.h"
#include "strata/base/diagnostics.h"

namespace strata::gfx::vk
{

std::unique_ptr<VkGpuDevice> VkGpuDevice::create(base::Diagnostics&                 diagnostics,
                                                 rhi::DeviceCreateInfo const&       info,
                                                 strata::platform::WsiHandle const& surface)
{
    (void)info; // later: debug flags, frames-in-flight, etc.

    auto dev          = std::unique_ptr<VkGpuDevice>(new VkGpuDevice());
    dev->diagnostics_ = &diagnostics;

    // Push diagnostics into wrappers that wnat to log (explicit, still no globals)
    dev->device_.set_diagnostics(&diagnostics);
    dev->swapchain_.set_diagnostics(&diagnostics);
    dev->command_pool_.set_diagnostics(&diagnostics);

    // 1) Instance + surface
    if (!dev->instance_.init(diagnostics, surface))
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "vk", "VkInstanceWrapper::init failed");
        return {};
    }

    // 2) Physical + logical device + queues
    if (!dev->device_.init(dev->instance_.instance(), dev->instance_.surface()))
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "vk", "VkDeviceWrapper::init failed");
        return {};
    }

    // 3) Command pool
    if (!dev->command_pool_.init(dev->device_.device(), dev->device_.graphics_family()))
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "vk", "VkCommandBufferPool::init failed");
        return {};
    }

    // 4) Frames-in-flight ring
    dev->frames_in_flight_ = 2; // start with 2
    if (!dev->init_frames())
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "vk", "VkGpuDevice::init_frames failed");
        return {};
    }

    // Basic pipeline is created after the first swapchain is created, because
    // it needs the swapchain color format. For now, we defer it to create_pipeline.

    STRATA_LOG_INFO(diagnostics.logger(), "vk", "VkGpuDevice created");
    return dev;
}

VkGpuDevice::~VkGpuDevice()
{
    // Be nice and let the GPU finish.
    wait_idle();

    // Destroy pipeline first (it holds the device handle)
    basic_pipeline_ = BasicPipeline{};

    // Descriptors must be destroyed/reset while VkDevice is still alive
    cleanup_descriptors();

    // Buffers
    cleanup_buffers();

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

// --- Synchronization -----------------------------------------------------

void VkGpuDevice::wait_idle()
{
    if (device_.device() != VK_NULL_HANDLE)
    {
        VkResult const res = vkDeviceWaitIdle(device_.device());
        if (res != VK_SUCCESS)
        {
            if (diagnostics_)
            {
                diagnostics_->logger().log(base::LogLevel::Error,
                                           "vk",
                                           vk_error_message("vkDeviceWaitIdle", res),
                                           std::source_location{});
            }
        }
    }
}

} // namespace strata::gfx::vk
