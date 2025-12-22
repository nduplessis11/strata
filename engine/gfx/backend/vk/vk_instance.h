// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_instance.h
//
// Purpose:
//   Declare a Vulkan instance wrapper with surface creation.
// -----------------------------------------------------------------------------

#pragma once

#include "strata/platform/wsi_handle.h"
#include <vulkan/vulkan.h>

namespace strata::gfx::vk
{

class VkInstanceWrapper
{
  public:
    VkInstanceWrapper() = default;
    ~VkInstanceWrapper();

    VkInstanceWrapper(VkInstanceWrapper&&) noexcept;
    VkInstanceWrapper& operator=(VkInstanceWrapper&&) noexcept;

    // Create instance and surface for a given WSI handle.
    bool init(strata::platform::WsiHandle const& wsi);

    VkInstance instance() const noexcept
    {
        return instance_;
    }
    VkSurfaceKHR surface() const noexcept
    {
        return surface_;
    }

  private:
    void cleanup();

    VkInstance               instance_{VK_NULL_HANDLE};
    VkSurfaceKHR             surface_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debug_messenger_{VK_NULL_HANDLE};
};

} // namespace strata::gfx::vk
