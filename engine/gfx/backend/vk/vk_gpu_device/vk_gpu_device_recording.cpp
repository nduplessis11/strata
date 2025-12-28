// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_recording.cpp
//
// Purpose:
//   Recording commands issued to Vulkan command buffers.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include "strata/base/diagnostics.h"

namespace strata::gfx::vk
{

namespace
{

inline VkImageLayout safe_old_layout(std::vector<VkImageLayout> const& layouts,
                                     std::uint32_t                     image_index)
{
    return (image_index < layouts.size()) ? layouts[image_index] : VK_IMAGE_LAYOUT_UNDEFINED;
}

inline bool aspect_has_stencil(VkImageAspectFlags aspect) noexcept
{
    return (aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
}

} // namespace

rhi::FrameResult VkGpuDevice::cmd_bind_descriptor_set(rhi::CommandBufferHandle /*cmd*/,
                                                      rhi::PipelineHandle      pipeline,
                                                      std::uint32_t            set_index,
                                                      rhi::DescriptorSetHandle set)
{
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;
    auto& diag = *diagnostics_;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
        return FrameResult::Error;

    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;
    if (vk_cmd == VK_NULL_HANDLE || !device_.device())
        return FrameResult::Error;

    STRATA_ASSERT_MSG(diag, pipeline, "cmd_bind_descriptor_set: invalid PipelineHandle");
    STRATA_ASSERT_MSG(diag, set, "cmd_bind_descriptor_set: invalid DescriptorSetHandle");

    if (!pipeline || !set)
        return FrameResult::Error;

    STRATA_ASSERT_MSG(diag,
                      basic_pipeline_.valid(),
                      "cmd_bind_descriptor_set: bind pipeline before binding sets");

    if (!basic_pipeline_.valid())
        return FrameResult::Error;

    VkDescriptorSet const vk_set = get_vk_descriptor_set(set);
    if (vk_set == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.record",
                         "cmd_bind_descriptor_set: descriptor set not found");
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    if (set_index >= pipeline_set_layout_handles_.size())
    {
        STRATA_LOG_ERROR(
            diag.logger(),
            "vk.record",
            "cmd_bind_descriptor_set: set_index {} out of range (pipeline has {} sets)",
            set_index,
            pipeline_set_layout_handles_.size());
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    vkCmdBindDescriptorSets(vk_cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            basic_pipeline_.layout,
                            set_index,
                            1,
                            &vk_set,
                            0,
                            nullptr);

    return FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::cmd_begin_swapchain_pass(
    [[maybe_unused]] rhi::CommandBufferHandle cmd,
    [[maybe_unused]] rhi::SwapchainHandle     swapchain,
    std::uint32_t                             image_index,
    rhi::ClearColor const&                    clear,
    [[maybe_unused]] rhi::TextureHandle       depth_texture,
    [[maybe_unused]] float                    clear_depth,
    [[maybe_unused]] std::uint32_t            clear_stencil)
{
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;

    auto& diag = *diagnostics_;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
        return FrameResult::Error;

    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

    if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid())
        return FrameResult::Error;

    auto const& images = swapchain_.images();
    auto const& views  = swapchain_.image_views();

    if (image_index >= images.size() || image_index >= views.size())
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.record",
                         "cmd_begin_swapchain_pass: image_index out of range");
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    bool const pipeline_expects_depth = (basic_pipeline_depth_format_ != VK_FORMAT_UNDEFINED);

    if (pipeline_expects_depth && !depth_texture)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.record",
                         "cmd_begin_swapchain_pass: pipeline expects depth format {} but no "
                         "depth_texture provided",
                         static_cast<std::int32_t>(basic_pipeline_depth_format_));
        diag.debug_break_on_error();
        return FrameResult::Error;
    }
    if (!pipeline_expects_depth && depth_texture)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.record",
                         "cmd_begin_swapchain_pass: depth_texture provided but pipeline expects no "
                         "depth (VK_FORMAT_UNDEFINED)");
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    VkImage     image = images[image_index];
    VkImageView view  = views[image_index];

    // Optional depth attachment
    VkImage            depth_image      = VK_NULL_HANDLE;
    VkImageView        depth_view       = VK_NULL_HANDLE;
    VkImageAspectFlags depth_aspect     = 0;
    VkImageLayout      depth_old_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (depth_texture)
    {
        std::size_t const tindex = static_cast<std::size_t>(depth_texture.value - 1);
        if (tindex >= textures_.size())
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.record",
                             "cmd_begin_swapchain_pass: depth_texture handle out of range");
            diag.debug_break_on_error();
            return FrameResult::Error;
        }

        TextureRecord const& trec = textures_[tindex];

        if (trec.format != basic_pipeline_depth_format_)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.record",
                             "Depth format mismatch: texture format {} != pipeline depth format {}",
                             static_cast<std::int32_t>(trec.format),
                             static_cast<std::int32_t>(basic_pipeline_depth_format_));
            diag.debug_break_on_error();
            return FrameResult::Error;
        }

        depth_image      = trec.image;
        depth_view       = trec.view;
        depth_aspect     = trec.aspect_mask;
        depth_old_layout = trec.layout;

        if (depth_image == VK_NULL_HANDLE || depth_view == VK_NULL_HANDLE)
        {
            STRATA_LOG_ERROR(
                diag.logger(),
                "vk.record",
                "cmd_begin_swapchain_pass: depth_texture is invalid (no VkImage/VkImageView");
            diag.debug_break_on_error();
            return FrameResult::Error;
        }

        if ((depth_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.record",
                             "cmd_begin_swapchain_pass: depth_texture does not have DEPTH aspect");
            diag.debug_break_on_error();
            return FrameResult::Error;
        }
    }

    // --- Barriers: swapchain color + optional depth --------------------------
    VkImageLayout old_layout = safe_old_layout(swapchain_image_layouts_, image_index);

    VkPipelineStageFlags2 src_stage2  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        src_access2 = 0;

    // Current model: UNDEFINED first use, PRESENT_SRC thereafter.
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
        STRATA_LOG_WARN(diag.logger(),
                        "vk.record",
                        "cmd_begin_swapchain_pass: unexpected old layout {}; treating as UNDEFINED",
                        static_cast<std::int32_t>(old_layout));
        old_layout  = VK_IMAGE_LAYOUT_UNDEFINED;
        src_stage2  = VK_PIPELINE_STAGE_2_NONE;
        src_access2 = 0;
    }

    VkImageMemoryBarrier2 barriers[2]{};

    barriers[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[0].srcStageMask        = src_stage2;
    barriers[0].srcAccessMask       = src_access2;
    barriers[0].dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout           = old_layout;
    barriers[0].newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image               = image;
    barriers[0].subresourceRange    = {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .baseMipLevel   = 0,
                                       .levelCount     = 1,
                                       .baseArrayLayer = 0,
                                       .layerCount     = 1};

    std::uint32_t barrier_count{1};

    // Depth barrier if provided
    if (depth_texture)
    {
        VkPipelineStageFlags2 depth_src_stage  = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2        depth_src_access = 0;

        if (depth_old_layout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            depth_src_stage  = VK_PIPELINE_STAGE_2_NONE;
            depth_src_access = 0;
        }
        else if (depth_old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            depth_src_stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            depth_src_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }
        else
        {
            // v1: we only expect UNDEFINED or DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
            // Be permissive but log once.
            STRATA_LOG_WARN(
                diag.logger(),
                "vk.record",
                "cmd_begin_swapchain_pass: depth old layout {} unexpected; treating as UNDEFINED",
                static_cast<std::int32_t>(depth_old_layout));
            depth_old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            depth_src_stage  = VK_PIPELINE_STAGE_2_NONE;
            depth_src_access = 0;
        }

        barriers[1].sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barriers[1].srcStageMask  = depth_src_stage;
        barriers[1].srcAccessMask = depth_src_access;
        barriers[1].dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        barriers[1].oldLayout           = depth_old_layout;
        barriers[1].newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].image               = depth_image;
        barriers[1].subresourceRange    = {.aspectMask     = depth_aspect,
                                           .baseMipLevel   = 0,
                                           .levelCount     = 1,
                                           .baseArrayLayer = 0,
                                           .layerCount     = 1};

        barrier_count = 2;
    }

    VkDependencyInfo dep_pre{};
    dep_pre.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_pre.imageMemoryBarrierCount = barrier_count;
    dep_pre.pImageMemoryBarriers    = barriers;

    vkCmdPipelineBarrier2(vk_cmd, &dep_pre);

    // Track depth layout locally once we have recorded the transition.
    if (depth_texture)
    {
        set_vk_image_layout(depth_texture, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    // --- Begin Rendering -----------------------------------------------------
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

    VkRenderingAttachmentInfo  depth_attach{};
    VkRenderingAttachmentInfo* depth_attach_ptr   = nullptr;
    VkRenderingAttachmentInfo* stencil_attach_ptr = nullptr;

    if (depth_texture)
    {
        VkClearValue depth_clear{};
        depth_clear.depthStencil.depth   = clear_depth;
        depth_clear.depthStencil.stencil = clear_stencil;

        depth_attach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attach.imageView   = depth_view;
        depth_attach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attach.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attach.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attach.clearValue  = depth_clear;

        depth_attach_ptr = &depth_attach;

        if (aspect_has_stencil(depth_aspect))
        {
            // For combined depth/stencil formats we can point both to the same attachment info.
            stencil_attach_ptr = &depth_attach;
        }
    }

    VkExtent2D const extent = swapchain_.extent();

    VkRenderingInfo render_info{};
    render_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    render_info.renderArea.offset    = {0, 0};
    render_info.renderArea.extent    = extent;
    render_info.layerCount           = 1;
    render_info.colorAttachmentCount = 1;
    render_info.pColorAttachments    = &color_attach;
    render_info.pDepthAttachment     = depth_attach_ptr;
    render_info.pStencilAttachment   = stencil_attach_ptr;

    vkCmdBeginRendering(vk_cmd, &render_info);
    return FrameResult::Ok;
}

rhi::FrameResult VkGpuDevice::cmd_end_swapchain_pass(
    [[maybe_unused]] rhi::CommandBufferHandle cmd,
    [[maybe_unused]] rhi::SwapchainHandle     swapchain,
    std::uint32_t                             image_index)
{
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;
    auto& diag = *diagnostics_;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
        return FrameResult::Error;

    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;
    if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid())
        return FrameResult::Error;

    auto const& images = swapchain_.images();
    if (image_index >= images.size())
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.record",
                         "cmd_end_swapchain_pass: image_index out of range");
        diag.debug_break_on_error();
        return FrameResult::Error;
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
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;
    auto& diag = *diagnostics_;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
        return FrameResult::Error;

    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

    if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid())
        return FrameResult::Error;

    STRATA_ASSERT_MSG(diag, pipeline, "cmd_bind_pipeline: invalid PipelineHandle");
    if (!pipeline)
        return FrameResult::Error;

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
                STRATA_LOG_ERROR(diag.logger(),
                                 "vk.record",
                                 "cmd_bind_pipeline: cannot rebuild pipeline (set layout invalid)");
                diag.debug_break_on_error();
                return FrameResult::Error;
            }
            vk_layouts.push_back(vk_layout);
        }

        basic_pipeline_ = create_basic_pipeline(device_.device(),
                                                swapchain_.image_format(),
                                                &diag,
                                                std::span{vk_layouts},
                                                basic_pipeline_depth_format_,
                                                basic_pipeline_depth_test_,
                                                basic_pipeline_depth_write_);
        if (!basic_pipeline_.valid())
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.record",
                             "cmd_bind_pipeline: failed to create BasicPipeline");
            diag.debug_break_on_error();
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
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;

    if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size())
        return FrameResult::Error;

    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;
    if (vk_cmd == VK_NULL_HANDLE)
        return FrameResult::Error;

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
        return FrameResult::Error;

    VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;
    if (vk_cmd == VK_NULL_HANDLE)
        return FrameResult::Error;

    vkCmdDraw(vk_cmd, vertex_count, instance_count, first_vertex, first_instance);
    return FrameResult::Ok;
}

} // namespace strata::gfx::vk
