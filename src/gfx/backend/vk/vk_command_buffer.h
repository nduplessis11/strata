#pragma once

#include <vulkan/vulkan.h>

#include "gfx/rhi/gpu_types.h"

namespace strata::gfx::vk {

class VkCommandBufferPool {
public:
    VkCommandBufferPool() = default;
    ~VkCommandBufferPool();

    VkCommandBufferPool(VkCommandBufferPool&&) noexcept;
    VkCommandBufferPool& operator=(VkCommandBufferPool&&) noexcept;

    bool init(VkDevice device);
    void cleanup(VkDevice device);

    VkCommandBuffer allocate(VkDevice device);

private:
    VkCommandPool pool_{ VK_NULL_HANDLE };
};

} // namespace strata::gfx::vk
