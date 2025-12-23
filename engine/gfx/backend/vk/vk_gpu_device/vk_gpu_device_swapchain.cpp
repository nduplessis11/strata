// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_swapchain.cpp
//
// Purpose:
//   Swapchain creation, resizing, image acquire, and present.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <limits>
#include <print>

namespace strata::gfx::vk
{

namespace
{
constexpr std::uint64_t fence_timeout = std::numeric_limits<std::uint64_t>::max();
}

rhi::SwapchainHandle VkGpuDevice::create_swapchain(rhi::SwapchainDesc const& desc,
                                                   strata::platform::WsiHandle const&)
{
    // We have only one swapchain; ignore the handle value and always use {1}.
    if (!device_.device())
    {
        return rhi::SwapchainHandle{};
    }

    wait_idle();
    swapchain_.cleanup();
    swapchain_image_layouts_.clear();

    if (!swapchain_.init(device_.physical(),
                         device_.device(),
                         instance_.surface(),
                         device_.graphics_family(),
                         device_.present_family(),
                         desc))
    {
        return rhi::SwapchainHandle{};
    }

    std::size_t const image_count = swapchain_.images().size();

    swapchain_image_layouts_.assign(image_count, VK_IMAGE_LAYOUT_UNDEFINED);
    images_in_flight_.assign(image_count, VK_NULL_HANDLE);

    if (!init_render_finished_per_image(image_count))
    {
        swapchain_.cleanup();
        swapchain_image_layouts_.clear();
        images_in_flight_.clear();
        return rhi::SwapchainHandle{};
    }

    // Pipeline is created separately via create_pipeline().
    return rhi::SwapchainHandle{1};
}

rhi::FrameResult VkGpuDevice::resize_swapchain(rhi::SwapchainHandle, rhi::SwapchainDesc const& desc)
{
    if (!device_.device())
    {
        return rhi::FrameResult::Error;
    }

    wait_idle();
    swapchain_.cleanup();
    swapchain_image_layouts_.clear();

    if (!swapchain_.init(device_.physical(),
                         device_.device(),
                         instance_.surface(),
                         device_.graphics_family(),
                         device_.present_family(),
                         desc))
    {
        return rhi::FrameResult::Error;
    }

    std::size_t const image_count = swapchain_.images().size();

    swapchain_image_layouts_.assign(image_count, VK_IMAGE_LAYOUT_UNDEFINED);
    images_in_flight_.assign(image_count, VK_NULL_HANDLE);

    if (!init_render_finished_per_image(image_count))
    {
        swapchain_.cleanup();
        swapchain_image_layouts_.clear();
        images_in_flight_.clear();
        return rhi::FrameResult::Error;
    }

    // The renderer will recreate the pipeline after resize via Render2D.
    basic_pipeline_ = BasicPipeline{};

    return rhi::FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::acquire_next_image(rhi::SwapchainHandle, rhi::AcquiredImage& out)
{
    using rhi::FrameResult;

    if (!swapchain_.valid() || !device_.device())
        return FrameResult::Error;
    if (frames_.empty())
        return FrameResult::Error;

    VkDevice         vk_device = device_.device();
    FrameSlot const& frame     = frames_[frame_index_];

    // Wait for this frame slot to be available
    if (vkWaitForFences(vk_device, 1, &frame.in_flight, VK_TRUE, fence_timeout) != VK_SUCCESS)
    {
        return FrameResult::Error;
    }

    // Acquire using per-frame semaphore
    std::uint32_t  image_index    = 0;
    VkResult const acquire_result = vkAcquireNextImageKHR(vk_device,
                                                          swapchain_.swapchain(),
                                                          fence_timeout,
                                                          frame.image_available,
                                                          VK_NULL_HANDLE,
                                                          &image_index);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
        return FrameResult::ResizeNeeded;
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR)
        return FrameResult::Error;

    // Wait if this swapchain image is still in flight
    if (image_index < images_in_flight_.size())
    {
        VkFence img_fence = images_in_flight_[image_index];
        if (img_fence != VK_NULL_HANDLE)
        {
            if (vkWaitForFences(vk_device, 1, &img_fence, VK_TRUE, fence_timeout) != VK_SUCCESS)
            {
                return FrameResult::Error;
            }
        }
        images_in_flight_[image_index] = frame.in_flight;
    }

    VkExtent2D const extent = swapchain_.extent();
    out.image_index         = image_index;
    out.extent              = rhi::Extent2D{extent.width, extent.height};
    out.frame_index         = frame_index_;

    return (acquire_result == VK_SUBOPTIMAL_KHR) ? FrameResult::Suboptimal : FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::present(rhi::SwapchainHandle, std::uint32_t image_index)
{
    using rhi::FrameResult;

    if (!swapchain_.valid() || !device_.device())
        return FrameResult::Error;
    if (image_index >= swapchain_sync_.render_finished_per_image.size())
        return FrameResult::Error;

    VkSemaphore    render_finished = swapchain_sync_.render_finished_per_image[image_index];
    VkSwapchainKHR sw              = swapchain_.swapchain();

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &render_finished;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sw;
    pi.pImageIndices      = &image_index;

    VkResult const pres = vkQueuePresentKHR(device_.present_queue(), &pi);

    if (pres == VK_ERROR_OUT_OF_DATE_KHR)
        return FrameResult::ResizeNeeded;
    if (pres == VK_SUBOPTIMAL_KHR)
        return FrameResult::Suboptimal;
    if (pres != VK_SUCCESS)
        return FrameResult::Error;

    return FrameResult::Ok;
}

bool VkGpuDevice::init_render_finished_per_image(std::size_t image_count)
{
    VkDevice vk_device = device_.device();
    if (!vk_device)
        return false;

    // destroy old
    for (auto s : swapchain_sync_.render_finished_per_image)
    {
        if (s != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(vk_device, s, nullptr);
        }
    }

    swapchain_sync_.render_finished_per_image.clear();
    swapchain_sync_.render_finished_per_image.resize(image_count, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (auto& sem : swapchain_sync_.render_finished_per_image)
    {
        if (vkCreateSemaphore(vk_device, &sem_ci, nullptr, &sem) != VK_SUCCESS)
        {
            std::println(stderr,
                         "VkGpuDevice: failed to create per-image render_finished semaphore");
            return false;
        }
    }

    return true;
}

void VkGpuDevice::destroy_render_finished_per_image()
{
    VkDevice vk_device = device_.device();
    if (!vk_device)
    {
        swapchain_sync_.render_finished_per_image.clear();
        return;
    }

    for (auto s : swapchain_sync_.render_finished_per_image)
    {
        if (s != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(vk_device, s, nullptr);
        }
    }
    swapchain_sync_.render_finished_per_image.clear();
}

} // namespace strata::gfx::vk
