// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_device.h
//
// Purpose:
//   Declare a Vulkan device wrapper for logical device and queues.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <limits>
#include <vulkan/vulkan.h>

namespace strata::gfx::vk
{

class VkDeviceWrapper
{
  public:
    VkDeviceWrapper() = default;
    ~VkDeviceWrapper();

    VkDeviceWrapper(VkDeviceWrapper&&) noexcept;
    VkDeviceWrapper& operator=(VkDeviceWrapper&&) noexcept;

    // Creates a logical device and queues for the given instance + surface.
    bool init(VkInstance instance, VkSurfaceKHR surface);
    void cleanup();

    [[nodiscard]] VkDevice device() const noexcept
    {
        return device_;
    }
    [[nodiscard]] VkPhysicalDevice physical() const noexcept
    {
        return physical_;
    }
    [[nodiscard]] std::uint32_t graphics_family() const noexcept
    {
        return graphics_family_;
    }
    [[nodiscard]] std::uint32_t present_family() const noexcept
    {
        return present_family_;
    }
    [[nodiscard]] VkQueue graphics_queue() const noexcept
    {
        return graphics_queue_;
    }
    [[nodiscard]] VkQueue present_queue() const noexcept
    {
        return present_queue_;
    }

  private:
    VkDevice         device_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    std::uint32_t    graphics_family_{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t    present_family_{std::numeric_limits<std::uint32_t>::max()};
    VkQueue          graphics_queue_{VK_NULL_HANDLE};
    VkQueue          present_queue_{VK_NULL_HANDLE};
};

} // namespace strata::gfx::vk
