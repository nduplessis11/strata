// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_swapchain.cpp
//
// Purpose:
//   Swapchain creation, resizing, image acquire, and present.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include "../vk_check.h"
#include "strata/base/diagnostics.h"

#include <limits>

namespace strata::gfx::vk
{

namespace
{

constexpr std::uint64_t fence_timeout = std::numeric_limits<std::uint64_t>::max();
}

rhi::SwapchainHandle VkGpuDevice::create_swapchain(rhi::SwapchainDesc const& desc,
                                                   strata::platform::WsiHandle const&)
{
    using namespace strata::base;

    if (!diagnostics_)
        return {};

    auto& diag = *diagnostics_;

    if (!device_.device())
    {
        return {};
    }

    wait_idle();

    VkSwapchainWrapper new_swapchain{};
    new_swapchain.set_diagnostics(diagnostics_);

    if (!new_swapchain.init(device_.physical(),
                            device_.device(),
                            instance_.surface(),
                            device_.graphics_family(),
                            device_.present_family(),
                            desc,
                            swapchain_.swapchain()))
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.swapchain",
                         "create_swapchain: VkSwapchainWrapper::init failed");
        return {};
    }

    std::size_t const image_count = new_swapchain.images().size();
    if (!init_render_finished_per_image(image_count))
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.swapchain",
                         "create_swapchain: init_render_finished_per_image failed");
        return {};
    }

    // Commit only after everything succeeds (swapchain + per-image sync).
    swapchain_ = std::move(new_swapchain);
    swapchain_image_layouts_.assign(image_count, VK_IMAGE_LAYOUT_UNDEFINED);
    images_in_flight_.assign(image_count, VK_NULL_HANDLE);

    STRATA_ASSERT_MSG(
        diag,
        swapchain_sync_.render_finished_per_image.size() == image_count,
        "create_swapchain: render_finished_per_image count must match swapchain images");

    return rhi::SwapchainHandle{1};
}

rhi::FrameResult VkGpuDevice::resize_swapchain(rhi::SwapchainHandle, rhi::SwapchainDesc const& desc)
{
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;

    auto& diag = *diagnostics_;

    if (!device_.device())
        return FrameResult::Error;

    wait_idle();

    // swapchain_.cleanup();
    // swapchain_image_layouts_.clear();
    // images_in_flight_.clear();
    // destroy_render_finished_per_image();

    // Best-effort recovery: if someone ended commands but never submitted, do not wedge.
    if (pending_submit_frame_index_ != invalid_index)
    {
        STRATA_LOG_WARN(
            diag.logger(),
            "vk.swapchain",
            "resize_swapchain: pending submit existed (slot={}); draining and discarding",
            pending_submit_frame_index_);

        (void)drain_image_available(pending_submit_frame_index_);
        pending_submit_frame_index_ = invalid_index;
        recording_active_           = false;
        recording_frame_index_      = invalid_index;

        // continue with resize
    }

    VkSwapchainWrapper new_swapchain{};
    new_swapchain.set_diagnostics(diagnostics_);

    if (!new_swapchain.init(device_.physical(),
                            device_.device(),
                            instance_.surface(),
                            device_.graphics_family(),
                            device_.present_family(),
                            desc,
                            swapchain_.swapchain()))
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.swapchain",
                         "resize_swapchain: VkSwapchainWrapper::init failed");
        return FrameResult::Error;
    }

    std::size_t const image_count = new_swapchain.images().size();
    if (!init_render_finished_per_image(image_count))
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.swapchain",
                         "resize_swapchain: init_render_finished_per_image failed");
        return FrameResult::Error;
    }

    // Commit only after everything succeeds.
    swapchain_ = std::move(new_swapchain);
    swapchain_image_layouts_.assign(image_count, VK_IMAGE_LAYOUT_UNDEFINED);
    images_in_flight_.assign(image_count, VK_NULL_HANDLE);

    STRATA_ASSERT_MSG(
        diag,
        swapchain_sync_.render_finished_per_image.size() == image_count,
        "resize_swapchain: render_finished_per_image count must match swapchain images");

    // Invalidate pipeline; renderer will recreate.
    basic_pipeline_ = BasicPipeline{};

    return FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::acquire_next_image(rhi::SwapchainHandle, rhi::AcquiredImage& out)
{
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;

    auto& diag = *diagnostics_;

    if (!swapchain_.valid() || !device_.device())
        return FrameResult::Error;
    if (frames_.empty())
        return FrameResult::Error;

    VkDevice         vk_device = device_.device();
    FrameSlot const& frame     = frames_[frame_index_];

    // Wait for this frame slot to be available
    VkResult const wr = vkWaitForFences(vk_device, 1, &frame.in_flight, VK_TRUE, fence_timeout);
    if (wr != VK_SUCCESS)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.swapchain",
                         "vkWaitForFences(frame.in_flight) failed: {}",
                         ::strata::gfx::vk::to_string(wr));
        diag.debug_break_on_error();
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
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.swapchain",
                         "vkAcquireNextImageKHR failed: {}",
                         ::strata::gfx::vk::to_string(acquire_result));
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    // Wait if this swapchain image is still in flight
    if (image_index < images_in_flight_.size())
    {
        VkFence img_fence = images_in_flight_[image_index];
        if (img_fence != VK_NULL_HANDLE)
        {
            VkResult const iw = vkWaitForFences(vk_device, 1, &img_fence, VK_TRUE, fence_timeout);
            if (iw != VK_SUCCESS)
            {
                STRATA_LOG_ERROR(diag.logger(),
                                 "vk.swapchain",
                                 "vkWaitForFences(images_in_flight_[{}]) failed: {}",
                                 image_index,
                                 ::strata::gfx::vk::to_string(iw));
                diag.debug_break_on_error();
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
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;

    auto& diag = *diagnostics_;

    if (!swapchain_.valid() || !device_.device())
        return FrameResult::Error;

    if (image_index >= swapchain_sync_.render_finished_per_image.size())
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.swapchain", "present: image_index out of range");
        return FrameResult::Error;
    }

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
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.swapchain",
                         "vkQueuePresentKHR failed: {}",
                         ::strata::gfx::vk::to_string(pres));
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    return FrameResult::Ok;
}

bool VkGpuDevice::init_render_finished_per_image(std::size_t image_count)
{
    using namespace strata::base;

    if (!diagnostics_)
        return false;

    auto& diag = *diagnostics_;

    VkDevice vk_device = device_.device();
    if (!vk_device)
        return false;

    // Build new semaphores in a temporary vector to avoid partial state leaks.
    std::vector<VkSemaphore> new_sems(image_count, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (std::size_t i = 0; i < image_count; ++i)
    {
        VkResult const r = vkCreateSemaphore(vk_device, &sem_ci, nullptr, &new_sems[i]);
        if (r != VK_SUCCESS)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.swapchain",
                             "vkCreateSemaphore(render_finished_per_image[{}]) failed: {}",
                             i,
                             ::strata::gfx::vk::to_string(r));
            diag.debug_break_on_error();

            for (auto s : new_sems)
            {
                if (s != VK_NULL_HANDLE)
                    vkDestroySemaphore(vk_device, s, nullptr);
            }
            return false;
        }
    }

    // Replace the old set.
    destroy_render_finished_per_image();
    swapchain_sync_.render_finished_per_image = std::move(new_sems);
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
