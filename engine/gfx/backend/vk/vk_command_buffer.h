// engine/gfx/backend/vk/vk_command_buffer.h
#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace strata::gfx::vk {

    class VkCommandBufferPool {
    public:
        VkCommandBufferPool() = default;
        ~VkCommandBufferPool();

        VkCommandBufferPool(VkCommandBufferPool&&) noexcept;
        VkCommandBufferPool& operator=(VkCommandBufferPool&&) noexcept;

        // Create a pool for a given queue family
        bool init(VkDevice device, std::uint32_t queue_family_index);
        void cleanup(VkDevice device);

        // Allocate a single primary command buffer from the pool
        VkCommandBuffer allocate(VkDevice device);

    private:
        VkCommandPool pool_{ VK_NULL_HANDLE };
    };

} // namespace strata::gfx::vk
