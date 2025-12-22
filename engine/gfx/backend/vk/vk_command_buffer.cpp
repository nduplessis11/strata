// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_command_buffer.cpp
//
// Purpose:
//   Implement Vulkan command buffer pool utilities.
// -----------------------------------------------------------------------------

#include "vk_command_buffer.h"

#include <vulkan/vulkan.h>

namespace strata::gfx::vk
{

VkCommandBufferPool::~VkCommandBufferPool() = default;

VkCommandBufferPool::VkCommandBufferPool(
    VkCommandBufferPool&& other) noexcept
{
    pool_       = other.pool_;
    other.pool_ = VK_NULL_HANDLE;
}

VkCommandBufferPool& VkCommandBufferPool::operator=(
    VkCommandBufferPool&& other) noexcept
{
    if (this != &other)
    {
        pool_       = other.pool_;
        other.pool_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkCommandBufferPool::init(
    VkDevice      device,
    std::uint32_t queue_family_index)
{
    // In case we're reinitializing
    cleanup(device);

    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = queue_family_index;

    if (vkCreateCommandPool(device, &ci, nullptr, &pool_) != VK_SUCCESS)
    {
        pool_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VkCommandBufferPool::cleanup(
    VkDevice device)
{
    if (pool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

VkCommandBuffer VkCommandBufferPool::allocate(
    VkDevice device)
{
    if (pool_ == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd{VK_NULL_HANDLE};
    if (vkAllocateCommandBuffers(device, &ai, &cmd) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return cmd;
}

} // namespace strata::gfx::vk
