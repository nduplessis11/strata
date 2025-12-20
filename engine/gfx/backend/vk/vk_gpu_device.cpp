// engine/gfx/backend/vk/vk_gpu_device.cpp

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

        bool init_frame_sync(FrameSync& fs, VkDevice device) {
            VkSemaphoreCreateInfo sem_ci{};
            sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            if (vkCreateSemaphore(device, &sem_ci, nullptr, &fs.image_available) != VK_SUCCESS) {
                std::println(stderr, "VkGpuDevice: failed to create image_available semaphore");
                return false;
            }

            VkFenceCreateInfo fence_ci{};
            fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            if (vkCreateFence(device, &fence_ci, nullptr, &fs.in_flight) != VK_SUCCESS) {
                std::println(stderr, "VkGpuDevice: failed to create in_flight fence");
                return false;
            }

            return true;
        }

        void destroy_frame_sync(FrameSync& fs, VkDevice device) {
            if (device == VK_NULL_HANDLE) return;

            if (fs.in_flight != VK_NULL_HANDLE) {
                vkDestroyFence(device, fs.in_flight, nullptr);
                fs.in_flight = VK_NULL_HANDLE;
            }

            if (fs.image_available != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, fs.image_available, nullptr);
                fs.image_available = VK_NULL_HANDLE;
            }
        }

        bool init_render_finished_per_image(FrameSync& fs, VkDevice device, std::size_t image_count) {
            // Destroy old if any (supports resize and recreate)
            for (auto s : fs.render_finished_per_image) {
                if (s != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, s, nullptr);
                }
            }

            fs.render_finished_per_image.clear();
            fs.render_finished_per_image.resize(image_count, VK_NULL_HANDLE);

            VkSemaphoreCreateInfo sem_ci{};
            sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            for (auto& sem : fs.render_finished_per_image) {
                if (vkCreateSemaphore(device, &sem_ci, nullptr, &sem) != VK_SUCCESS) {
                    std::println(stderr, "VkGpuDevice: failed to create per-image render_finished semaphore");
                    return false;
                }
            }

            return true;
        }

        void destroy_render_finished_per_image(FrameSync& fs, VkDevice device) {
            if (device == VK_NULL_HANDLE) return;

            for (auto s : fs.render_finished_per_image) {
                if (s != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, s, nullptr);
                }
            }
            fs.render_finished_per_image.clear();
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

        // 3) Command pool + single primary command buffer
        if (!dev->command_pool_.init(dev->device_.device(), dev->device_.graphics_family())) {
            return {};
        }

        dev->primary_cmd_ = dev->command_pool_.allocate(dev->device_.device());
        if (dev->primary_cmd_ == VK_NULL_HANDLE) {
            return {};
        }

        // 4) Frame sync (semaphores + fence)
        if (!init_frame_sync(dev->frame_sync_, dev->device_.device())) {
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
        destroy_render_finished_per_image(frame_sync_, device_.device());
        destroy_frame_sync(frame_sync_, device_.device());

        // Command pool + command buffer
        command_pool_.cleanup(device_.device());
        primary_cmd_ = VK_NULL_HANDLE;

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

        swapchain_image_layouts_.assign(swapchain_.images().size(), VK_IMAGE_LAYOUT_UNDEFINED);

        if (!init_render_finished_per_image(frame_sync_, device_.device(), swapchain_.images().size())) {
            swapchain_.cleanup();
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

        swapchain_image_layouts_.assign(swapchain_.images().size(), VK_IMAGE_LAYOUT_UNDEFINED);

        if (!init_render_finished_per_image(frame_sync_, device_.device(), swapchain_.images().size())) {
            swapchain_.cleanup();
            return rhi::FrameResult::Error;
        }

        // The renderer will recreate the pipeline after resize via Render2D.
        basic_pipeline_ = BasicPipeline{};

        return rhi::FrameResult::Ok;
    }

    rhi::FrameResult VkGpuDevice::present(rhi::SwapchainHandle) {
        using rhi::FrameResult;

        if (!swapchain_.valid() ||
            primary_cmd_ == VK_NULL_HANDLE || !device_.device()) {
            return FrameResult::Error;
        }

        // If the pipeline was invalidated by a resize, rebuild it now.
        if (!basic_pipeline_.valid()) {
            basic_pipeline_ = create_basic_pipeline(device_.device(), swapchain_.image_format());
            if (!basic_pipeline_.valid()) {
                std::println(stderr, "VkGpuDevice: failed to (re)create basic pipeline in present()");
                return FrameResult::Error;
            }
        }

        VkDevice device = device_.device();

        // Wait for previous frame to finish
        VkResult wait_res = vkWaitForFences(device, 1, &frame_sync_.in_flight, VK_TRUE, kFenceTimeout);
        if (wait_res != VK_SUCCESS) {
            std::println(stderr, "vkWaitForFences failed: {}", static_cast<int>(wait_res));
            return FrameResult::Error;
        }
        vkResetFences(device, 1, &frame_sync_.in_flight);

        // Acquire next image
        std::uint32_t image_index = 0;
        VkResult acquire_result = vkAcquireNextImageKHR(
            device,
            swapchain_.swapchain(),
            kFenceTimeout,
            frame_sync_.image_available,
            VK_NULL_HANDLE,
            &image_index);

        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
            return FrameResult::ResizeNeeded;
        }
        if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
            std::println(stderr, "vkAcquireNextImageKHR failed: {}", static_cast<int>(acquire_result));
            return FrameResult::Error;
        }

        // We still render on SUBOPTIMAL and let the caller decide to resize.
        vkResetCommandBuffer(primary_cmd_, 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = 0;

        if (vkBeginCommandBuffer(primary_cmd_, &begin) != VK_SUCCESS) {
            std::println(stderr, "vkBeginCommandBuffer failed");
            return FrameResult::Error;
        }

        const auto& images = swapchain_.images();
        const auto& views = swapchain_.image_views();
        VkExtent2D extent = swapchain_.extent();

        if (image_index >= images.size() || image_index >= views.size()) {
            std::println(stderr, "VkGpuDevice: image index out of range");
            vkEndCommandBuffer(primary_cmd_);
            return FrameResult::Error;
        }

        if (frame_sync_.render_finished_per_image.size() != images.size()) {
            std::println(stderr, "VkGpuDevice: render_finished_per_image not initialized");
            return FrameResult::Error;
        }
        VkSemaphore render_finished = frame_sync_.render_finished_per_image[image_index];

        VkImage     image = images[image_index];
        VkImageView view = views[image_index];

        if (swapchain_image_layouts_.size() != images.size()) {
            std::println(stderr, "VkGpuDevice: swapchain_image_layouts_ not initialized");
            vkEndCommandBuffer(primary_cmd_);
            return FrameResult::Error;
        }

        VkImageLayout old_layout = swapchain_image_layouts_[image_index];

        // Transition image to COLOR_ATTACHMENT_OPTIMAL
        VkImageMemoryBarrier pre{};
        pre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        pre.srcAccessMask = 0;
        pre.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        pre.oldLayout = old_layout;
        pre.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre.image = image;
        pre.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        pre.subresourceRange.baseMipLevel = 0;
        pre.subresourceRange.levelCount = 1;
        pre.subresourceRange.baseArrayLayer = 0;
        pre.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            primary_cmd_,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &pre);

        // Clear color & dynamic rendering setup
        VkClearValue clear{};
        clear.color = { { 0.6f, 0.4f, 0.8f, 1.0f } };

        VkRenderingAttachmentInfo color_attach{};
        color_attach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attach.imageView = view;
        color_attach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attach.clearValue = clear;
        color_attach.resolveMode = VK_RESOLVE_MODE_NONE;
        color_attach.resolveImageView = VK_NULL_HANDLE;
        color_attach.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkRenderingInfo render_info{};
        render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        render_info.renderArea.offset = { 0, 0 };
        render_info.renderArea.extent = extent;
        render_info.layerCount = 1;
        render_info.colorAttachmentCount = 1;
        render_info.pColorAttachments = &color_attach;
        render_info.pDepthAttachment = nullptr;
        render_info.pStencilAttachment = nullptr;

        vkCmdBeginRendering(primary_cmd_, &render_info);

        vkCmdBindPipeline(primary_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, basic_pipeline_.pipeline);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = extent;

        vkCmdSetViewport(primary_cmd_, 0, 1, &viewport);
        vkCmdSetScissor(primary_cmd_, 0, 1, &scissor);

        // Fullscreen triangle (no vertex buffer; vertex shader fabricates positions)
        vkCmdDraw(primary_cmd_, 3, 1, 0, 0);

        vkCmdEndRendering(primary_cmd_);

        // Transition image to PRESENT_SRC_KHR
        VkImageMemoryBarrier post{};
        post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        post.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        post.dstAccessMask = 0;
        post.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        post.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        post.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post.image = image;
        post.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        post.subresourceRange.baseMipLevel = 0;
        post.subresourceRange.levelCount = 1;
        post.subresourceRange.baseArrayLayer = 0;
        post.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            primary_cmd_,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &post);

        if (vkEndCommandBuffer(primary_cmd_) != VK_SUCCESS) {
            std::println(stderr, "vkEndCommandBuffer failed");
            return FrameResult::Error;
        }

        swapchain_image_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // Submit to graphics queue
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame_sync_.image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &primary_cmd_;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished;

        if (vkQueueSubmit(device_.graphics_queue(), 1, &submit, frame_sync_.in_flight) != VK_SUCCESS) {
            std::println(stderr, "vkQueueSubmit failed");
            return FrameResult::Error;
        }

        // Present
        VkSwapchainKHR sw = swapchain_.swapchain();

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &sw;
        present_info.pImageIndices = &image_index;
        present_info.pResults = nullptr;

        VkResult pres = vkQueuePresentKHR(device_.present_queue(), &present_info);

        if (pres == VK_ERROR_OUT_OF_DATE_KHR) {
            return FrameResult::ResizeNeeded;
        }
        if (pres == VK_SUBOPTIMAL_KHR) {
            return FrameResult::Suboptimal;
        }
        if (pres != VK_SUCCESS) {
            std::println(stderr, "vkQueuePresentKHR failed: {}", static_cast<int>(pres));
            return FrameResult::Error;
        }

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
        // Not used by Render2D yet; RHI has a simplified "present-only" path.
        // We still hand out unique handles so the API is complete.
        return allocate_command_handle();
    }

    void VkGpuDevice::end_commands(rhi::CommandBufferHandle) {
        // Stub
    }

    void VkGpuDevice::submit(const rhi::SubmitDesc&) {
        // Stub
    }

    // --- Synchronization -----------------------------------------------------

    void VkGpuDevice::wait_idle() {
        if (device_.device() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_.device());
        }
    }

} // namespace strata::gfx::vk
