#include "vulkan_device.h"

#include <cstdint>
#include <limits>
#include <print>
#include <utility>

namespace strata::gfx::vulkan {
namespace {
    constexpr std::uint64_t kFenceTimeout = std::numeric_limits<std::uint64_t>::max();
}

VulkanSwapchain::VulkanSwapchain(Swapchain swapchain)
    : swapchain_(std::move(swapchain)) {
}

VulkanPipeline::VulkanPipeline(vk::BasicPipeline pipeline)
    : pipeline(std::move(pipeline)) {
}

VulkanDevice::CommandResources::CommandResources(VkDevice device, std::uint32_t queue_family)
    : device(device) {
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = queue_family;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &pool_ci, nullptr, &pool) != VK_SUCCESS) {
        std::println(stderr, "VulkanDevice: failed to create command pool");
        pool = VK_NULL_HANDLE;
        return;
    }

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &alloc, &cmd) != VK_SUCCESS) {
        std::println(stderr, "VulkanDevice: failed to allocate command buffer");
        cmd = VK_NULL_HANDLE;
    }
}

VulkanDevice::CommandResources::~CommandResources() {
    if (!device) return;
    if (cmd != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, pool, 1, &cmd);
    }
    if (pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, pool, nullptr);
    }
}

VulkanDevice::CommandResources::CommandResources(CommandResources&& other) noexcept
    : device(other.device)
    , pool(other.pool)
    , cmd(other.cmd) {
    other.device = VK_NULL_HANDLE;
    other.pool = VK_NULL_HANDLE;
    other.cmd = VK_NULL_HANDLE;
}

VulkanDevice::CommandResources& VulkanDevice::CommandResources::operator=(CommandResources&& other) noexcept {
    if (this != &other) {
        this->~CommandResources();
        new (this) CommandResources();
        device = other.device;
        pool = other.pool;
        cmd = other.cmd;
        other.device = VK_NULL_HANDLE;
        other.pool = VK_NULL_HANDLE;
        other.cmd = VK_NULL_HANDLE;
    }
    return *this;
}

VulkanDevice::FrameSyncObjects::FrameSyncObjects(VkDevice device)
    : device(device) {
    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(device, &sem_ci, nullptr, &image_available) != VK_SUCCESS ||
        vkCreateSemaphore(device, &sem_ci, nullptr, &render_finished) != VK_SUCCESS) {
        std::println(stderr, "VulkanDevice: failed to create semaphores");
    }

    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateFence(device, &fence_ci, nullptr, &in_flight) != VK_SUCCESS) {
        std::println(stderr, "VulkanDevice: failed to create fence");
    }
}

VulkanDevice::FrameSyncObjects::~FrameSyncObjects() {
    if (!device) return;
    if (in_flight != VK_NULL_HANDLE) {
        vkDestroyFence(device, in_flight, nullptr);
    }
    if (image_available != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, image_available, nullptr);
    }
    if (render_finished != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, render_finished, nullptr);
    }
}

VulkanDevice::FrameSyncObjects::FrameSyncObjects(FrameSyncObjects&& other) noexcept
    : device(other.device)
    , image_available(other.image_available)
    , render_finished(other.render_finished)
    , in_flight(other.in_flight) {
    other.device = VK_NULL_HANDLE;
    other.image_available = VK_NULL_HANDLE;
    other.render_finished = VK_NULL_HANDLE;
    other.in_flight = VK_NULL_HANDLE;
}

VulkanDevice::FrameSyncObjects& VulkanDevice::FrameSyncObjects::operator=(FrameSyncObjects&& other) noexcept {
    if (this != &other) {
        this->~FrameSyncObjects();
        new (this) FrameSyncObjects();
        device = other.device;
        image_available = other.image_available;
        render_finished = other.render_finished;
        in_flight = other.in_flight;
        other.device = VK_NULL_HANDLE;
        other.image_available = VK_NULL_HANDLE;
        other.render_finished = VK_NULL_HANDLE;
        other.in_flight = VK_NULL_HANDLE;
    }
    return *this;
}

VulkanDevice::VulkanDevice(const strata::platform::WsiHandle& wsi, bool enable_validation) {
    VulkanContextDesc ctx_desc{};
    ctx_desc.enable_validation = enable_validation;
    context_ = VulkanContext::create(wsi, ctx_desc);
    if (!context_.valid() || !context_.has_device()) {
        std::println(stderr, "Failed to create VulkanDevice: invalid context");
        return;
    }

    commands_ = CommandResources(context_.device(), context_.graphics_family_index());
    sync_ = FrameSyncObjects(context_.device());
}

VulkanDevice::~VulkanDevice() {
    wait_idle();
}

std::unique_ptr<GraphicsSwapchain> VulkanDevice::create_swapchain(
    strata::platform::Extent2d size,
    GraphicsSwapchain* old_swapchain) {

    if (!context_.has_device()) {
        return nullptr;
    }

    VkSwapchainKHR old_handle{ VK_NULL_HANDLE };
    if (old_swapchain) {
        if (auto* vk_old = dynamic_cast<VulkanSwapchain*>(old_swapchain)) {
            old_handle = vk_old->handle();
        }
    }

    Swapchain swapchain = Swapchain::create(context_, size, old_handle);
    if (!swapchain.valid()) {
        return nullptr;
    }

    return std::make_unique<VulkanSwapchain>(std::move(swapchain));
}

std::unique_ptr<GraphicsPipeline> VulkanDevice::create_pipeline(const GraphicsSwapchain& swapchain) {
    if (!context_.has_device()) {
        return nullptr;
    }

    const auto* vk_swap = dynamic_cast<const VulkanSwapchain*>(&swapchain);
    if (!vk_swap) {
        return nullptr;
    }

    VkFormat color_format = static_cast<VkFormat>(vk_swap->color_format());
    VkExtent2D extent{ static_cast<uint32_t>(vk_swap->extent().width),
                       static_cast<uint32_t>(vk_swap->extent().height) };

    auto pipeline = vk::create_basic_pipeline(context_.device(), color_format, extent);
    if (!pipeline.valid()) {
        std::println(stderr, "VulkanDevice: failed to create pipeline");
        return nullptr;
    }

    return std::make_unique<VulkanPipeline>(std::move(pipeline));
}

FrameResult VulkanDevice::draw_frame(GraphicsSwapchain& swapchain_base, GraphicsPipeline* pipeline_base) {
    auto* swapchain = dynamic_cast<VulkanSwapchain*>(&swapchain_base);
    auto* pipeline = dynamic_cast<VulkanPipeline*>(pipeline_base);

    if (!swapchain || !pipeline || !swapchain->is_valid() || !pipeline->pipeline.valid() ||
        !commands_.cmd || !context_.has_device()) {
        return FrameResult::Error;
    }

    VkDevice device = context_.device();

    VkResult wait_res{ vkWaitForFences(device, 1, &sync_.in_flight, VK_TRUE, kFenceTimeout) };
    if (wait_res != VK_SUCCESS) {
        std::println(stderr, "vkWaitForFences failed: {}", static_cast<int>(wait_res));
        return FrameResult::Error;
    }
    vkResetFences(device, 1, &sync_.in_flight);

    uint32_t image_index = 0;
    VkResult acquire_result = vkAcquireNextImageKHR(
        device,
        swapchain->handle(),
        kFenceTimeout,
        sync_.image_available,
        VK_NULL_HANDLE,
        &image_index);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        return FrameResult::SwapchainOutOfDate;
    }
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
        std::println(stderr, "vkAcquireNextImageKHR failed: {}", static_cast<int>(acquire_result));
        return FrameResult::Error;
    }

    vkResetCommandBuffer(commands_.cmd, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = 0;

    if (vkBeginCommandBuffer(commands_.cmd, &begin) != VK_SUCCESS) {
        std::println(stderr, "vkBeginCommandBuffer failed");
        return FrameResult::Error;
    }

    auto images = swapchain->images();
    auto views = swapchain->image_views();
    auto extent = swapchain->extent();

    VkImage image = images[image_index];
    VkImageView view = views[image_index];

    VkImageMemoryBarrier pre{};
    pre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre.srcAccessMask = 0;
    pre.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    pre.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
        commands_.cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &pre);

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
    render_info.renderArea.extent = VkExtent2D{
        static_cast<uint32_t>(extent.width),
        static_cast<uint32_t>(extent.height)};
    render_info.layerCount = 1;
    render_info.colorAttachmentCount = 1;
    render_info.pColorAttachments = &color_attach;
    render_info.pDepthAttachment = nullptr;
    render_info.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commands_.cmd, &render_info);

    vkCmdBindPipeline(commands_.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline.pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = VkExtent2D{
        static_cast<uint32_t>(extent.width),
        static_cast<uint32_t>(extent.height)};

    vkCmdSetViewport(commands_.cmd, 0, 1, &viewport);
    vkCmdSetScissor(commands_.cmd, 0, 1, &scissor);
    vkCmdDraw(commands_.cmd, 3, 1, 0, 0);

    vkCmdEndRendering(commands_.cmd);

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
        commands_.cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &post);

    if (vkEndCommandBuffer(commands_.cmd) != VK_SUCCESS) {
        std::println(stderr, "vkEndCommandBuffer failed");
        return FrameResult::Error;
    }

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &sync_.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands_.cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &sync_.render_finished;

    if (vkQueueSubmit(context_.graphics_queue(), 1, &submit, sync_.in_flight) != VK_SUCCESS) {
        std::println(stderr, "vkQueueSubmit failed");
        return FrameResult::Error;
    }

    VkSwapchainKHR sw = swapchain->handle();

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &sync_.render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &sw;
    present.pImageIndices = &image_index;
    present.pResults = nullptr;

    VkResult pres = vkQueuePresentKHR(context_.present_queue(), &present);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        return FrameResult::SwapchainOutOfDate;
    }
    else if (pres != VK_SUCCESS) {
        std::println(stderr, "vkQueuePresentKHR failed: {}", static_cast<int>(pres));
        return FrameResult::Error;
    }
    return FrameResult::Ok;
}

void VulkanDevice::wait_idle() {
    if (context_.has_device()) {
        vkDeviceWaitIdle(context_.device());
    }
}

} // namespace strata::gfx::vulkan
