#pragma once

#include <vulkan/vulkan.h>

#include "platform/wsi_handle.h"

namespace strata::gfx::vk {

class VkInstanceWrapper {
public:
    VkInstanceWrapper() = default;
    ~VkInstanceWrapper();

    VkInstanceWrapper(VkInstanceWrapper&&) noexcept;
    VkInstanceWrapper& operator=(VkInstanceWrapper&&) noexcept;

    bool init(const strata::platform::WsiHandle& wsi);
    VkInstance   instance() const noexcept { return instance_; }
    VkSurfaceKHR surface()  const noexcept { return surface_; }

private:
    void cleanup();

    VkInstance   instance_{ VK_NULL_HANDLE };
    VkSurfaceKHR surface_{ VK_NULL_HANDLE };
};

} // namespace strata::gfx::vk
