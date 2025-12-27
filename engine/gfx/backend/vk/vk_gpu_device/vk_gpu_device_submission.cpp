// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_submission.cpp
//
// Purpose:
//   Command buffer submission and frames-in-flight handling.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include "../vk_check.h"
#include "strata/base/diagnostics.h"

namespace strata::gfx::vk
{

rhi::CommandBufferHandle VkGpuDevice::begin_commands()
{
    using namespace strata::base;

    if (!diagnostics_)
        return {};

    auto& diag = *diagnostics_;

    if (frames_.empty())
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.submit", "begin_commands: frames_ is empty");
        return {};
    }

    // Calling begin twice without finishing is a bug.
    STRATA_ASSERT_MSG(diag,
                      !recording_active_,
                      "begin_commands called while recording_active_ = true");

    // Lock the frame slot used for this recording.
    recording_frame_index_ = frame_index_;
    recording_active_      = true;

    FrameSlot const& frame = frames_[recording_frame_index_];
    if (frame.cmd == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.submit", "begin_commands: frame.cmd is VK_NULL_HANDLE");
        recording_active_ = false;
        return {};
    }

    STRATA_VK_ASSERT_RETURN(diag, vkResetCommandBuffer(frame.cmd, 0), rhi::CommandBufferHandle{});

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult const br = vkBeginCommandBuffer(frame.cmd, &begin);
    if (br != VK_SUCCESS)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.submit",
                         "vkBeginCommandBuffer failed: {}",
                         ::strata::gfx::vk::to_string(br));
        diag.debug_break_on_error();
        recording_active_ = false;
        return {};
    }

    // Still "opaque" for now; later encode recording_frame_index_ here.
    return rhi::CommandBufferHandle{1};
}

rhi::FrameResult VkGpuDevice::end_commands(rhi::CommandBufferHandle)
{
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;

    auto& diag = *diagnostics_;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.submit", "end_commands: invalid recording state");
        return FrameResult::Error;
    }

    FrameSlot const& frame = frames_[recording_frame_index_];
    if (frame.cmd == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.submit", "end_commands: frame.cmd is VK_NULL_HANDLE");
        recording_active_ = false;
        return FrameResult::Error;
    }

    VkResult const er = vkEndCommandBuffer(frame.cmd);
    if (er != VK_SUCCESS)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.submit",
                         "vkEndCommandBuffer failed: {}",
                         ::strata::gfx::vk::to_string(er));
        diag.debug_break_on_error();
        recording_active_ = false;
        return FrameResult::Error;
    }

    return FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::submit(rhi::IGpuDevice::SubmitDesc const& sd)
{
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;

    auto& diag = *diagnostics_;

    if (!device_.device() || !swapchain_.valid())
        return FrameResult::Error;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.submit", "submit: invalid recording state");
        return FrameResult::Error;
    }

    if (!sd.command_buffer)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.submit", "submit: sd.command_buffer is invalid");
        recording_active_ = false;
        return FrameResult::Error;
    }

    FrameSlot const& frame = frames_[recording_frame_index_];

    std::uint32_t const image_index = sd.image_index;
    if (image_index >= swapchain_sync_.render_finished_per_image.size())
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.submit", "submit: image_index out of range");
        recording_active_ = false;
        return FrameResult::Error;
    }

    VkSemaphore render_finished           = swapchain_sync_.render_finished_per_image[image_index];
    VkPipelineStageFlags const wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &frame.image_available;
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &frame.cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &render_finished;

    VkDevice vk_device = device_.device();

    STRATA_VK_ASSERT_RETURN(diag,
                            vkResetFences(vk_device, 1, &frame.in_flight),
                            FrameResult::Error);

    VkResult const qsr = vkQueueSubmit(device_.graphics_queue(), 1, &submit, frame.in_flight);
    if (qsr != VK_SUCCESS)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.submit",
                         "vkQueueSubmit failed: {}",
                         ::strata::gfx::vk::to_string(qsr));
        diag.debug_break_on_error();
        recording_active_ = false;
        return FrameResult::Error;
    }

    if (image_index < swapchain_image_layouts_.size())
    {
        swapchain_image_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    recording_active_ = false;

    // Advance frame slot for the NEXT frame
    frame_index_ = (frame_index_ + 1) % frames_in_flight_;

    return FrameResult::Ok;
}

bool VkGpuDevice::init_frames()
{
    using namespace strata::base;

    if (!diagnostics_)
        return false;

    auto& diag = *diagnostics_;

    if (!device_.device())
        return false;

    // Ensure clean state even on re-init
    destroy_frames();

    frames_.resize(frames_in_flight_);

    VkDevice vk_device = device_.device();

    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::uint32_t i = 0; i < frames_in_flight_; ++i)
    {
        frames_[i].cmd = command_pool_.allocate(vk_device);
        if (frames_[i].cmd == VK_NULL_HANDLE)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.submit",
                             "init_frames: failed to allocate cmd buffer (i={})",
                             i);
            destroy_frames();
            return false;
        }

        VkResult const sr =
            vkCreateSemaphore(vk_device, &sem_ci, nullptr, &frames_[i].image_available);
        if (sr != VK_SUCCESS)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.submit",
                             "init_frames: vkCreateSemaphore(image_available) failed: {}",
                             ::strata::gfx::vk::to_string(sr));
            destroy_frames();
            return false;
        }

        VkResult const fr = vkCreateFence(vk_device, &fence_ci, nullptr, &frames_[i].in_flight);
        if (fr != VK_SUCCESS)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.submit",
                             "init_frames: vkCreateFence(in_flight) failed: {}",
                             ::strata::gfx::vk::to_string(fr));
            destroy_frames();
            return false;
        }
    }

    frame_index_ = 0;
    return true;
}

void VkGpuDevice::destroy_frames()
{
    VkDevice vk_device = device_.device();
    if (!vk_device)
    {
        frames_.clear();
        return;
    }

    for (auto& f : frames_)
    {
        if (f.in_flight != VK_NULL_HANDLE)
        {
            vkDestroyFence(vk_device, f.in_flight, nullptr);
        }
        if (f.image_available != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(vk_device, f.image_available, nullptr);
        }
        // command buffers freed with pool destruction/reset
        f = {};
    }
    frames_.clear();
}

} // namespace strata::gfx::vk
