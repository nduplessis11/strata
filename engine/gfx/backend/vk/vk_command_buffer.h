// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_command_buffer.h
//
// Purpose:
//   Declare a Vulkan command buffer pool wrapper.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace strata::base
{
class Diagnostics;
}

namespace strata::gfx::vk
{

class VkCommandBufferPool
{
  public:
    VkCommandBufferPool() = default;
    ~VkCommandBufferPool();

    VkCommandBufferPool(VkCommandBufferPool&&) noexcept;
    VkCommandBufferPool& operator=(VkCommandBufferPool&&) noexcept;

    // Explicit injection (no globals). Safe to call multiple times.
    void set_diagnostics(base::Diagnostics* diagnostics) noexcept
    {
        diagnostics_ = diagnostics;
    }

    // Create a pool for a given queue family
    bool init(VkDevice device, std::uint32_t queue_family_index);
    void cleanup(VkDevice device);

    // Allocate a single primary command buffer from the pool
    VkCommandBuffer allocate(VkDevice device);

  private:
    base::Diagnostics* diagnostics_{nullptr}; // non-owning
    VkCommandPool      pool_{VK_NULL_HANDLE};
};

} // namespace strata::gfx::vk
