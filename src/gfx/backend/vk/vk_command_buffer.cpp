#include "gfx/backend/vk/vk_command_buffer.h"

namespace strata::gfx::vk {

VkCommandBufferPool::~VkCommandBufferPool() = default;

VkCommandBufferPool::VkCommandBufferPool(VkCommandBufferPool&& other) noexcept {
    pool_ = other.pool_;
    other.pool_ = VK_NULL_HANDLE;
}

VkCommandBufferPool& VkCommandBufferPool::operator=(VkCommandBufferPool&& other) noexcept {
    if (this != &other) {
        pool_ = other.pool_;
        other.pool_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkCommandBufferPool::init(VkDevice) {
    // Stub: would create command pool
    pool_ = VK_NULL_HANDLE;
    return true;
}

void VkCommandBufferPool::cleanup(VkDevice) {
    // Stub
    pool_ = VK_NULL_HANDLE;
}

VkCommandBuffer VkCommandBufferPool::allocate(VkDevice) {
    // Stub: would allocate command buffer
    return VK_NULL_HANDLE;
}

} // namespace strata::gfx::vk
