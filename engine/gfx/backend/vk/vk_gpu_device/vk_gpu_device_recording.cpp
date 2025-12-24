// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_recording.cpp
//
// Purpose:
//   Recording commands issued to Vulkan command buffers.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <print>

namespace strata::gfx::vk
{

namespace
{
inline VkImageLayout safe_old_layout(std::vector<VkImageLayout> const& layouts,
                                     std::uint32_t                     image_index)
{
    return (image_index < layouts.size()) ? layouts[image_index] : VK_IMAGE_LAYOUT_UNDEFINED;
}
} // namespace

rhi::FrameResult VkGpuDevice::cmd_bind_descriptor_set(rhi::CommandBufferHandle /*cmd*/,
                                                      rhi::PipelineHandle /*pipeline*/,
                                                      std::uint32_t /*set_index*/,
                                                      rhi::DescriptorSetHandle /*set*/)
{
    // Stub
    std::println(stderr, "VkGpuDevice: cmd_bind_descriptor_set not implemented");
    return rhi::FrameResult::Error;
}

rhi::FrameResult VkGpuDevice::cmd_begin_swapchain_pass(
    [[maybe_unused]] rhi::CommandBufferHandle cmd,
    [[maybe_unused]] rhi::SwapchainHandle     swapchain,
    std::uint32_t                             image_index,
    rhi::ClearColor const&                    clear)
{

    using rhi::FrameResult;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
    {
        return FrameResult::Error;
    }
    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

    if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid())
    {
        std::println(stderr, "cmd_begin_swapchain_pass: invalid device/swapchain/cmd");
        return FrameResult::Error;
    }

    auto const& images = swapchain_.images();
    auto const& views  = swapchain_.image_views();

    if (image_index >= images.size() || image_index >= views.size())
    {
        std::println(stderr, "cmd_begin_swapchain_pass: image_index out of range");
        return FrameResult::Error;
    }

    VkImage     image = images[image_index];
    VkImageView view  = views[image_index];

    // --- Pre barrier: oldLayout -> COLOR_ATTACHMENT_OPTIMAL ----------------
    VkImageLayout old_layout = safe_old_layout(swapchain_image_layouts_, image_index);

    VkPipelineStageFlags2 src_stage2  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        src_access2 = 0;

    // Current model: UNDEFINED first use, PRESENT_SRC thereafter.
    // Be defensive to keep validation happy during transitions/refactors.
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        src_stage2  = VK_PIPELINE_STAGE_2_NONE;
        src_access2 = 0;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        src_stage2  = VK_PIPELINE_STAGE_2_NONE;
        src_access2 = 0;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        // Not expected in today's model, but safe enough if it happens.
        src_stage2  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        src_access2 = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else
    {
        std::println(stderr,
                     "cmd_begin_swapchain_pass: unexpected old layout {}, treating as UNDEFINED",
                     static_cast<std::int32_t>(old_layout));
        old_layout  = VK_IMAGE_LAYOUT_UNDEFINED;
        src_stage2  = VK_PIPELINE_STAGE_2_NONE;
        src_access2 = 0;
    }

    VkImageMemoryBarrier2 pre2{};
    pre2.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    pre2.srcStageMask        = src_stage2;
    pre2.srcAccessMask       = src_access2;
    pre2.dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    pre2.dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    pre2.oldLayout           = old_layout;
    pre2.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    pre2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre2.image               = image;
    pre2.subresourceRange    = {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1};

    VkDependencyInfo dep_pre{};
    dep_pre.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_pre.imageMemoryBarrierCount = 1;
    dep_pre.pImageMemoryBarriers    = &pre2;

    vkCmdPipelineBarrier2(vk_cmd, &dep_pre);

    // Clear color & dynamic rendering setup
    VkClearValue clear_value{};
    clear_value.color = {{clear.r, clear.g, clear.b, clear.a}};

    VkRenderingAttachmentInfo color_attach{};
    color_attach.sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attach.imageView          = view;
    color_attach.imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attach.loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attach.storeOp            = VK_ATTACHMENT_STORE_OP_STORE;
    color_attach.clearValue         = clear_value;
    color_attach.resolveMode        = VK_RESOLVE_MODE_NONE;
    color_attach.resolveImageView   = VK_NULL_HANDLE;
    color_attach.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkExtent2D const extent = swapchain_.extent();

    VkRenderingInfo render_info{};
    render_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    render_info.renderArea.offset    = {0, 0};
    render_info.renderArea.extent    = extent;
    render_info.layerCount           = 1;
    render_info.colorAttachmentCount = 1;
    render_info.pColorAttachments    = &color_attach;
    render_info.pDepthAttachment     = nullptr;
    render_info.pStencilAttachment   = nullptr;

    vkCmdBeginRendering(vk_cmd, &render_info);

    return FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::cmd_end_swapchain_pass(
    [[maybe_unused]] rhi::CommandBufferHandle cmd,
    [[maybe_unused]] rhi::SwapchainHandle     swapchain,
    std::uint32_t                             image_index)
{

    using rhi::FrameResult;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
    {
        return FrameResult::Error;
    }
    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

    if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid())
    {
        std::println(stderr, "cmd_end_swapchain_pass: invalid device/swapchain/cmd");
        return rhi::FrameResult::Error;
    }

    auto const& images = swapchain_.images();
    if (image_index >= images.size())
    {
        std::println(stderr, "cmd_end_swapchain_pass: image_index out of range");
        return rhi::FrameResult::Error;
    }

    VkImage image = images[image_index];

    vkCmdEndRendering(vk_cmd);

    // Transition image to PRESENT_SRC_KHR
    VkImageMemoryBarrier2 post2{};
    post2.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    post2.srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    post2.srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    post2.dstStageMask        = VK_PIPELINE_STAGE_2_NONE;
    post2.dstAccessMask       = 0;
    post2.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    post2.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    post2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post2.image               = image;
    post2.subresourceRange    = {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel   = 0,
                                 .levelCount     = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount     = 1};

    VkDependencyInfo dep_post{};
    dep_post.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_post.imageMemoryBarrierCount = 1;
    dep_post.pImageMemoryBarriers    = &post2;

    vkCmdPipelineBarrier2(vk_cmd, &dep_post);

    // IMPORTANT: don't update swapchain_image_layouts_ here.
    // Update it after successful vkQueueSubmit in submit().
    return rhi::FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::cmd_bind_pipeline([[maybe_unused]] rhi::CommandBufferHandle cmd,
                                                rhi::PipelineHandle                       pipeline)
{

    using rhi::FrameResult;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
    {
        return FrameResult::Error;
    }
    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

    if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid())
    {
        std::println(stderr, "cmd_bind_pipeline: invalid device/swapchain/cmd");
        return FrameResult::Error;
    }
    if (!pipeline)
    {
        std::println(stderr, "cmd_bind_pipeline: invalid PipelineHandle");
        return FrameResult::Error;
    }

    // Lazily rebuild backend pipeline if needed (e.g., after swapchain resize).
    if (!basic_pipeline_.valid())
    {
        std::vector<VkDescriptorSetLayout> vk_layouts;
        vk_layouts.reserve(pipeline_set_layout_handles_.size());

        for (auto const h : pipeline_set_layout_handles_)
        {
            VkDescriptorSetLayout const vk_layout = get_vk_descriptor_set_layout(h);
            if (vk_layout == VK_NULL_HANDLE)
            {
                std::println(stderr,
                             "cmd_bind_pipeline: cannot rebuild pipeline (set layout invalid)");
                return FrameResult::Error;
            }
            vk_layouts.push_back(vk_layout);
        }

        basic_pipeline_ = create_basic_pipeline(device_.device(),
                                                swapchain_.image_format(),
                                                std::span{vk_layouts});
        if (!basic_pipeline_.valid())
        {
            std::println(stderr, "cmd_bind_pipeline: failed to (re)create BasicPipeline");
            return FrameResult::Error;
        }
    }

    vkCmdBindPipeline(vk_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, basic_pipeline_.pipeline);
    return FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::cmd_set_viewport_scissor(
    [[maybe_unused]] rhi::CommandBufferHandle cmd,
    rhi::Extent2D                             extent)
{

    using rhi::FrameResult;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
    {
        return FrameResult::Error;
    }
    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

    if (vk_cmd == VK_NULL_HANDLE)
    {
        std::println(stderr, "cmd_set_viewport_scissor: invalid cmd");
        return FrameResult::Error;
    }

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(extent.width);
    viewport.height   = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {extent.width, extent.height};

    vkCmdSetViewport(vk_cmd, 0, 1, &viewport);
    vkCmdSetScissor(vk_cmd, 0, 1, &scissor);

    return FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::cmd_draw([[maybe_unused]] rhi::CommandBufferHandle cmd,
                                       std::uint32_t                             vertex_count,
                                       std::uint32_t                             instance_count,
                                       std::uint32_t                             first_vertex,
                                       std::uint32_t                             first_instance)
{

    using rhi::FrameResult;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
    {
        return FrameResult::Error;
    }
    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

    if (vk_cmd == VK_NULL_HANDLE)
    {
        std::println(stderr, "cmd_draw: invalid cmd");
        return FrameResult::Error;
    }

    vkCmdDraw(vk_cmd, vertex_count, instance_count, first_vertex, first_instance);

    return FrameResult::Ok;
}

} // namespace strata::gfx::vk
