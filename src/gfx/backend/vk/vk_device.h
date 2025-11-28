#pragma once

#include <vulkan/vulkan.h>

namespace strata::gfx::vk {

class VkDeviceWrapper {
public:
    VkDeviceWrapper() = default;
    ~VkDeviceWrapper();

    VkDeviceWrapper(VkDeviceWrapper&&) noexcept;
    VkDeviceWrapper& operator=(VkDeviceWrapper&&) noexcept;

    bool init(VkInstance instance);
    void cleanup();

    VkDevice device() const noexcept { return device_; }

private:
    VkDevice device_{ VK_NULL_HANDLE };
};

} // namespace strata::gfx::vk
