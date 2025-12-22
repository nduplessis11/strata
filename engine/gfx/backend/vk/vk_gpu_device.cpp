// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device.cpp
//
// Purpose:
//   Implement the Vulkan IGpuDevice backend and RHI factory selection.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <limits>
#include <print>

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

    namespace {
        constexpr std::uint64_t kFenceTimeout = std::numeric_limits<std::uint64_t>::max();

        inline VkImageLayout safe_old_layout(const std::vector<VkImageLayout>& layouts, std::uint32_t image_index) {
            return (image_index < layouts.size()) ? layouts[image_index] : VK_IMAGE_LAYOUT_UNDEFINED;
        }
    } // anonymous namespace

    std::unique_ptr<VkGpuDevice> VkGpuDevice::create(
        const rhi::DeviceCreateInfo& info,
        const strata::platform::WsiHandle& surface)
    {
        (void)info; // later: debug flags, frames-in-flight, etc.

        auto dev = std::unique_ptr<VkGpuDevice>(new VkGpuDevice());

        // 1) Instance + surface
        if (!dev->instance_.init(surface)) {
            return {};
        }

        // 2) Physical + logical device + queues
        if (!dev->device_.init(dev->instance_.instance(), dev->instance_.surface())) {
            return {};
        }

        // 3) Command pool
        if (!dev->command_pool_.init(dev->device_.device(), dev->device_.graphics_family())) {
            return {};
        }

        // 4) Frames-in-flight ring
        dev->frames_in_flight_ = 2; // start with 2
        if (!dev->init_frames()) {
            return {};
        }

        // Basic pipeline is created after the first swapchain is created, because
        // it needs the swapchain color format. For now, we defer it to create_pipeline.

        return dev;
    }

    VkGpuDevice::~VkGpuDevice() {
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

    // --- Swapchain -----------------------------------------------------------

    rhi::SwapchainHandle VkGpuDevice::create_swapchain(
        const rhi::SwapchainDesc& desc,
        const strata::platform::WsiHandle&)
    {
        // We have only one swapchain; ignore the handle value and always use {1}.
        if (!device_.device()) {
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
            desc)) {
            return rhi::SwapchainHandle{};
        }

        const std::size_t image_count = swapchain_.images().size();

        swapchain_image_layouts_.assign(image_count, VK_IMAGE_LAYOUT_UNDEFINED);
        images_in_flight_.assign(image_count, VK_NULL_HANDLE);

        if (!init_render_finished_per_image(image_count)) {
            swapchain_.cleanup();
            swapchain_image_layouts_.clear();
            images_in_flight_.clear();
            return rhi::SwapchainHandle{};
        }

        // Pipeline is created separately via create_pipeline().
        return rhi::SwapchainHandle{ 1 };
    }

    rhi::FrameResult VkGpuDevice::resize_swapchain(
        rhi::SwapchainHandle,
        const rhi::SwapchainDesc& desc)
    {
        if (!device_.device()) {
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
            desc)) {
            return rhi::FrameResult::Error;
        }

        const std::size_t image_count = swapchain_.images().size();

        swapchain_image_layouts_.assign(image_count, VK_IMAGE_LAYOUT_UNDEFINED);
        images_in_flight_.assign(image_count, VK_NULL_HANDLE);

        if (!init_render_finished_per_image(image_count)) {
            swapchain_.cleanup();
            swapchain_image_layouts_.clear();
            images_in_flight_.clear();
            return rhi::FrameResult::Error;
        }

        // The renderer will recreate the pipeline after resize via Render2D.
        basic_pipeline_ = BasicPipeline{};

        return rhi::FrameResult::Ok;
    }

    rhi::FrameResult VkGpuDevice::acquire_next_image(rhi::SwapchainHandle, rhi::AcquiredImage& out) {
        using rhi::FrameResult;

        if (!swapchain_.valid() || !device_.device()) return FrameResult::Error;
        if (frames_.empty()) return FrameResult::Error;

        VkDevice vk_device = device_.device();
        FrameSlot& frame = frames_[frame_index_];

        // Wait for this frame slot to be available
        if (vkWaitForFences(vk_device, 1, &frame.in_flight, VK_TRUE, kFenceTimeout) != VK_SUCCESS) {
            return FrameResult::Error;
        }

        // Acquire using per-frame semaphore
        std::uint32_t image_index = 0;
        VkResult ar = vkAcquireNextImageKHR(
            vk_device,
            swapchain_.swapchain(),
            kFenceTimeout,
            frame.image_available,
            VK_NULL_HANDLE,
            &image_index);

        if (ar == VK_ERROR_OUT_OF_DATE_KHR) return FrameResult::ResizeNeeded;
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) return FrameResult::Error;

        // Wait if this swapchain image is still in flight
        if (image_index < images_in_flight_.size()) {
            VkFence img_fence = images_in_flight_[image_index];
            if (img_fence != VK_NULL_HANDLE) {
                if (vkWaitForFences(vk_device, 1, &img_fence, VK_TRUE, kFenceTimeout) != VK_SUCCESS) {
                    return FrameResult::Error;
                }
            }
            images_in_flight_[image_index] = frame.in_flight;
        }

        VkExtent2D extent = swapchain_.extent();
        out.image_index = image_index;
        out.extent = rhi::Extent2D{ extent.width, extent.height };
        out.frame_index = frame_index_;

        return (ar == VK_SUBOPTIMAL_KHR) ? FrameResult::Suboptimal : FrameResult::Ok;
    }

    rhi::FrameResult VkGpuDevice::present(rhi::SwapchainHandle, std::uint32_t image_index) {
        using rhi::FrameResult;

        if (!swapchain_.valid() || !device_.device()) return FrameResult::Error;
        if (image_index >= swapchain_sync_.render_finished_per_image.size()) return FrameResult::Error;

        VkSemaphore render_finished = swapchain_sync_.render_finished_per_image[image_index];
        VkSwapchainKHR sw = swapchain_.swapchain();

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &render_finished;
        pi.swapchainCount = 1;
        pi.pSwapchains = &sw;
        pi.pImageIndices = &image_index;

        VkResult pres = vkQueuePresentKHR(device_.present_queue(), &pi);

        if (pres == VK_ERROR_OUT_OF_DATE_KHR) return FrameResult::ResizeNeeded;
        if (pres == VK_SUBOPTIMAL_KHR)        return FrameResult::Suboptimal;
        if (pres != VK_SUCCESS)               return FrameResult::Error;

        return FrameResult::Ok;
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
        // Not used by Render2D yet; just hand out a unique handle for now.
        return allocate_buffer_handle();
    }

    void VkGpuDevice::destroy_buffer(rhi::BufferHandle) {
        // Stub for now
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
        if (!swapchain_.valid() || !device_.device()) {
            return rhi::PipelineHandle{};
        }

        // We ignore PipelineDesc for now and always build the same fullscreen-triangle pipeline.
        basic_pipeline_ = create_basic_pipeline(device_.device(), swapchain_.image_format());
        if (!basic_pipeline_.valid()) {
            std::println(stderr, "VkGpuDevice: failed to create basic pipeline");
            return rhi::PipelineHandle{};
        }

        return allocate_pipeline_handle();
    }

    void VkGpuDevice::destroy_pipeline(rhi::PipelineHandle) {
        // We only keep one backend pipeline; drop it when asked to destroy any handle.
        basic_pipeline_ = BasicPipeline{};
    }

    // --- Commands & submission ----------------------------------------------

    rhi::CommandBufferHandle VkGpuDevice::begin_commands() {
        if (frames_.empty()) return {};

        // Lock the frame slot used for this recording.
        recording_frame_index_ = frame_index_;
        recording_active_ = true;

        FrameSlot& frame = frames_[recording_frame_index_];
        if (frame.cmd == VK_NULL_HANDLE) return {};

        vkResetCommandBuffer(frame.cmd, 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(frame.cmd, &begin) != VK_SUCCESS) {
            recording_active_ = false;
            return {};
        }

        // Still "opaque" for now; later encode recording_frame_index_ here.
        return rhi::CommandBufferHandle{ 1 };
    }

    rhi::FrameResult VkGpuDevice::end_commands(rhi::CommandBufferHandle) {
        using rhi::FrameResult;

        if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size()) {
            return FrameResult::Error;
        }

        FrameSlot& frame = frames_[recording_frame_index_];
        if (frame.cmd == VK_NULL_HANDLE) return FrameResult::Error;

        return (vkEndCommandBuffer(frame.cmd) == VK_SUCCESS) ? FrameResult::Ok : FrameResult::Error;
    }

    rhi::FrameResult VkGpuDevice::submit(const rhi::IGpuDevice::SubmitDesc& sd) {
        using rhi::FrameResult;

        if (!device_.device() || !swapchain_.valid()) return FrameResult::Error;
        if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size()) return FrameResult::Error;
        if (!sd.command_buffer) return FrameResult::Error;

        FrameSlot& frame = frames_[recording_frame_index_];

        const std::uint32_t image_index = sd.image_index;
        if (image_index >= swapchain_sync_.render_finished_per_image.size()) return FrameResult::Error;

        VkSemaphore render_finished = swapchain_sync_.render_finished_per_image[image_index];
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame.image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame.cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished;

        VkDevice vk_device = device_.device();
        if (vkResetFences(vk_device, 1, &frame.in_flight) != VK_SUCCESS) return FrameResult::Error;
        if (vkQueueSubmit(device_.graphics_queue(), 1, &submit, frame.in_flight) != VK_SUCCESS) return FrameResult::Error;

        if (image_index < swapchain_image_layouts_.size()) {
            swapchain_image_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        // Recording is done for this frame.
        recording_active_ = false;

        // Advance frame slot for the NEXT frame
        frame_index_ = (frame_index_ + 1) % frames_in_flight_;

        return FrameResult::Ok;
    }

    // --- Synchronization -----------------------------------------------------

    void VkGpuDevice::wait_idle() {
        if (device_.device() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_.device());
        }
    }

    // --- Recording (explicit functions fine for now) --------------------------

    rhi::FrameResult VkGpuDevice::cmd_begin_swapchain_pass(
        [[maybe_unused]] rhi::CommandBufferHandle cmd,
        [[maybe_unused]] rhi::SwapchainHandle swapchain,
        std::uint32_t image_index,
        const rhi::ClearColor& clear) {

        using rhi::FrameResult;

        if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size()) {
            return FrameResult::Error;
        }
        VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

        if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid()) {
            std::println(stderr, "cmd_begin_swapchain_pass: invalid device/swapchain/cmd");
            return FrameResult::Error;
        }

        const auto& images = swapchain_.images();
        const auto& views = swapchain_.image_views();

        if (image_index >= images.size() || image_index >= views.size()) {
            std::println(stderr, "cmd_begin_swapchain_pass: image_index out of range");
            return FrameResult::Error;
        }

        VkImage     image = images[image_index];
        VkImageView view = views[image_index];

        // --- Pre barrier: oldLayout -> COLOR_ATTACHMENT_OPTIMAL ----------------
        VkImageLayout old_layout = safe_old_layout(swapchain_image_layouts_, image_index);

        VkPipelineStageFlags2 src_stage2 = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2        src_access2 = 0;

        // Current model: UNDEFINED first use, PRESENT_SRC thereafter.
        // Be defensive to keep validation happy during transitions/refactors.
        if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            src_stage2 = VK_PIPELINE_STAGE_2_NONE;
            src_access2 = 0;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            src_stage2 = VK_PIPELINE_STAGE_2_NONE;
            src_access2 = 0;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            // Not expected in today's model, but safe enough if it happens.
            src_stage2 = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            src_access2 = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else {
            std::println(stderr,
                "cmd_begin_swapchain_pass: unexpected old layout {}, treating as UNDEFINED",
                static_cast<std::int32_t>(old_layout));
            old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            src_stage2 = VK_PIPELINE_STAGE_2_NONE;
            src_access2 = 0;
        }

        VkImageMemoryBarrier2 pre2{};
        pre2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        pre2.srcStageMask = src_stage2;
        pre2.srcAccessMask = src_access2;
        pre2.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        pre2.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        pre2.oldLayout = old_layout;
        pre2.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        pre2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre2.image = image;
        pre2.subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        VkDependencyInfo dep_pre{};
        dep_pre.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_pre.imageMemoryBarrierCount = 1;
        dep_pre.pImageMemoryBarriers = &pre2;

        vkCmdPipelineBarrier2(vk_cmd, &dep_pre);

        // Clear color & dynamic rendering setup
        VkClearValue clear_value{};
        clear_value.color = { { clear.r, clear.g, clear.b, clear.a } };

        VkRenderingAttachmentInfo color_attach{};
        color_attach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attach.imageView = view;
        color_attach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attach.clearValue = clear_value;
        color_attach.resolveMode = VK_RESOLVE_MODE_NONE;
        color_attach.resolveImageView = VK_NULL_HANDLE;
        color_attach.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkExtent2D extent = swapchain_.extent();

        VkRenderingInfo render_info{};
        render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        render_info.renderArea.offset = { 0, 0 };
        render_info.renderArea.extent = extent;
        render_info.layerCount = 1;
        render_info.colorAttachmentCount = 1;
        render_info.pColorAttachments = &color_attach;
        render_info.pDepthAttachment = nullptr;
        render_info.pStencilAttachment = nullptr;

        vkCmdBeginRendering(vk_cmd, &render_info);

        return FrameResult::Ok;
    }

    rhi::FrameResult VkGpuDevice::cmd_end_swapchain_pass(
        [[maybe_unused]] rhi::CommandBufferHandle cmd,
        [[maybe_unused]] rhi::SwapchainHandle swapchain,
        std::uint32_t image_index) {

        using rhi::FrameResult;

        if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size()) {
            return FrameResult::Error;
        }
        VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

        if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid()) {
            std::println(stderr, "cmd_end_swapchain_pass: invalid device/swapchain/cmd");
            return rhi::FrameResult::Error;
        }

        const auto& images = swapchain_.images();
        if (image_index >= images.size()) {
            std::println(stderr, "cmd_end_swapchain_pass: image_index out of range");
            return rhi::FrameResult::Error;
        }

        VkImage image = images[image_index];

        vkCmdEndRendering(vk_cmd);

        // Transition image to PRESENT_SRC_KHR
        VkImageMemoryBarrier2 post2{};
        post2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        post2.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        post2.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        post2.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        post2.dstAccessMask = 0;
        post2.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        post2.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        post2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post2.image = image;
        post2.subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        VkDependencyInfo dep_post{};
        dep_post.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_post.imageMemoryBarrierCount = 1;
        dep_post.pImageMemoryBarriers = &post2;

        vkCmdPipelineBarrier2(vk_cmd, &dep_post);

        // IMPORTANT: don't update swapchain_image_layouts_ here.
        // Update it after successful vkQueueSubmit in submit().
        return rhi::FrameResult::Ok;
    }

    rhi::FrameResult VkGpuDevice::cmd_bind_pipeline(
        [[maybe_unused]] rhi::CommandBufferHandle cmd,
        rhi::PipelineHandle pipeline) {

        using rhi::FrameResult;

        if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size()) {
            return FrameResult::Error;
        }
        VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

        if (vk_cmd == VK_NULL_HANDLE || !device_.device() || !swapchain_.valid()) {
            std::println(stderr, "cmd_bind_pipeline: invalid device/swapchain/cmd");
            return FrameResult::Error;
        }
        if (!pipeline) {
            std::println(stderr, "cmd_bind_pipeline: invalid PipelineHandle");
            return FrameResult::Error;
        }

        // Lazily rebuild backend pipeline if needed (e.g., after swapchain resize).
        if (!basic_pipeline_.valid()) {
            basic_pipeline_ = create_basic_pipeline(device_.device(), swapchain_.image_format());
            if (!basic_pipeline_.valid()) {
                std::println(stderr, "cmd_bind_pipeline: failed to (re)create BasicPipeline");
                return FrameResult::Error;
            }
        }

        vkCmdBindPipeline(vk_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, basic_pipeline_.pipeline);
        return FrameResult::Ok;
    }

    rhi::FrameResult VkGpuDevice::cmd_set_viewport_scissor(
        [[maybe_unused]] rhi::CommandBufferHandle cmd,
        rhi::Extent2D extent) {

        using rhi::FrameResult;

        if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size()) {
            return FrameResult::Error;
        }
        VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

        if (vk_cmd == VK_NULL_HANDLE) {
            std::println(stderr, "cmd_set_viewport_scissor: invalid cmd");
            return FrameResult::Error;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { extent.width, extent.height };

        vkCmdSetViewport(vk_cmd, 0, 1, &viewport);
        vkCmdSetScissor(vk_cmd, 0, 1, &scissor);

        return FrameResult::Ok;
    }

    rhi::FrameResult VkGpuDevice::cmd_draw(
        [[maybe_unused]] rhi::CommandBufferHandle cmd,
        std::uint32_t vertex_count,
        std::uint32_t instance_count,
        std::uint32_t first_vertex,
        std::uint32_t first_instance) {

        using rhi::FrameResult;

        if (!recording_active_ || frames_.empty() || recording_frame_index_ >= frames_.size()) {
            return FrameResult::Error;
        }
        VkCommandBuffer vk_cmd = frames_[recording_frame_index_].cmd;

        if (vk_cmd == VK_NULL_HANDLE) {
            std::println(stderr, "cmd_draw: invalid cmd");
            return FrameResult::Error;
        }

        vkCmdDraw(vk_cmd, vertex_count, instance_count, first_vertex, first_instance);

        return FrameResult::Ok;
    }

    bool VkGpuDevice::init_frames() {
        if (!device_.device()) return false;

        frames_.clear();
        frames_.resize(frames_in_flight_);

        VkDevice vk_device = device_.device();

        VkSemaphoreCreateInfo sem_ci{};
        sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_ci{};
        fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (std::uint32_t i = 0; i < frames_in_flight_; ++i) {
            frames_[i].cmd = command_pool_.allocate(vk_device);
            if (frames_[i].cmd == VK_NULL_HANDLE) {
                std::println(stderr, "VkGpuDevice: failed to allocate frame cmd buffer");
                return false;
            }

            if (vkCreateSemaphore(vk_device, &sem_ci, nullptr, &frames_[i].image_available) != VK_SUCCESS) {
                std::println(stderr, "VkGpuDevice: failed to create frame image_available semaphore");
                return false;
            }

            if (vkCreateFence(vk_device, &fence_ci, nullptr, &frames_[i].in_flight) != VK_SUCCESS) {
                std::println(stderr, "VkGpuDevice: failed to create frame in_flight fence");
                return false;
            }
        }

        frame_index_ = 0;
        return true;
    }

    void VkGpuDevice::destroy_frames() {
        VkDevice vk_device = device_.device();
        if (!vk_device) {
            frames_.clear();
            return;
        }

        for (auto& f : frames_) {
            if (f.in_flight != VK_NULL_HANDLE) {
                vkDestroyFence(vk_device, f.in_flight, nullptr);
            }
            if (f.image_available != VK_NULL_HANDLE) {
                vkDestroySemaphore(vk_device, f.image_available, nullptr);
            }
            // command buffers freed with pool destruction/reset
            f = {};
        }
        frames_.clear();
    }

    bool VkGpuDevice::init_render_finished_per_image(std::size_t image_count) {
        VkDevice vk_device = device_.device();
        if (!vk_device) return false;

        // destroy old
        for (auto s : swapchain_sync_.render_finished_per_image) {
            if (s != VK_NULL_HANDLE) {
                vkDestroySemaphore(vk_device, s, nullptr);
            }
        }

        swapchain_sync_.render_finished_per_image.clear();
        swapchain_sync_.render_finished_per_image.resize(image_count, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo sem_ci{};
        sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (auto& sem : swapchain_sync_.render_finished_per_image) {
            if (vkCreateSemaphore(vk_device, &sem_ci, nullptr, &sem) != VK_SUCCESS) {
                std::println(stderr, "VkGpuDevice: failed to create per-image render_finished semaphore");
                return false;
            }
        }

        return true;
    }

    void VkGpuDevice::destroy_render_finished_per_image() {
        VkDevice vk_device = device_.device();
        if (!vk_device) {
            swapchain_sync_.render_finished_per_image.clear();
            return;
        }

        for (auto s : swapchain_sync_.render_finished_per_image) {
            if (s != VK_NULL_HANDLE) {
                vkDestroySemaphore(vk_device, s, nullptr);
            }
        }
        swapchain_sync_.render_finished_per_image.clear();
    }

} // namespace strata::gfx::vk
