// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_submission.cpp
//
// Purpose:
//   Command buffer submission and frames-in-flight handling.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <print>

namespace strata::gfx::vk
{

rhi::CommandBufferHandle VkGpuDevice::begin_commands()
{
    if (frames_.empty())
        return {};

    // Lock the frame slot used for this recording.
    recording_frame_index_ = frame_index_;
    recording_active_      = true;

    FrameSlot const& frame = frames_[recording_frame_index_];
    if (frame.cmd == VK_NULL_HANDLE)
        return {};

    vkResetCommandBuffer(frame.cmd, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(frame.cmd, &begin) != VK_SUCCESS)
    {
        recording_active_ = false;
        return {};
    }

    // Still "opaque" for now; later encode recording_frame_index_ here.
    return rhi::CommandBufferHandle{1};
}

rhi::FrameResult VkGpuDevice::end_commands(rhi::CommandBufferHandle)
{
    using rhi::FrameResult;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
    {
        return FrameResult::Error;
    }

    FrameSlot const& frame = frames_[recording_frame_index_];
    if (frame.cmd == VK_NULL_HANDLE)
        return FrameResult::Error;

    return (vkEndCommandBuffer(frame.cmd) == VK_SUCCESS) ? FrameResult::Ok : FrameResult::Error;
}

rhi::FrameResult VkGpuDevice::submit(rhi::IGpuDevice::SubmitDesc const& sd)
{
    using rhi::FrameResult;

    if (!device_.device() || !swapchain_.valid())
        return FrameResult::Error;
    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
        return FrameResult::Error;
    if (!sd.command_buffer)
        return FrameResult::Error;

    FrameSlot const& frame = frames_[recording_frame_index_];

    std::uint32_t const image_index = sd.image_index;
    if (image_index >= swapchain_sync_.render_finished_per_image.size())
        return FrameResult::Error;

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
    if (vkResetFences(vk_device, 1, &frame.in_flight) != VK_SUCCESS)
        return FrameResult::Error;
    if (vkQueueSubmit(device_.graphics_queue(), 1, &submit, frame.in_flight) != VK_SUCCESS)
        return FrameResult::Error;

    if (image_index < swapchain_image_layouts_.size())
    {
        swapchain_image_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    // Recording is done for this frame.
    recording_active_ = false;

    // Advance frame slot for the NEXT frame
    frame_index_ = (frame_index_ + 1) % frames_in_flight_;

    return FrameResult::Ok;
}

bool VkGpuDevice::init_frames()
{
    if (!device_.device())
        return false;

    frames_.clear();
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
            std::println(stderr, "VkGpuDevice: failed to allocate frame cmd buffer");
            return false;
        }

        if (vkCreateSemaphore(vk_device, &sem_ci, nullptr, &frames_[i].image_available) !=
            VK_SUCCESS)
        {
            std::println(stderr, "VkGpuDevice: failed to create frame image_available semaphore");
            return false;
        }

        if (vkCreateFence(vk_device, &fence_ci, nullptr, &frames_[i].in_flight) != VK_SUCCESS)
        {
            std::println(stderr, "VkGpuDevice: failed to create frame in_flight fence");
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
