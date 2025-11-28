#pragma once

#include <vulkan/vulkan.h>

#include "gfx/rhi/gpu_types.h"

namespace strata::gfx::vk {

class VkSwapchainWrapper {
public:
    VkSwapchainWrapper() = default;
    ~VkSwapchainWrapper();

    VkSwapchainWrapper(VkSwapchainWrapper&&) noexcept;
    VkSwapchainWrapper& operator=(VkSwapchainWrapper&&) noexcept;

    bool init(VkDevice device, const rhi::SwapchainDesc& desc);
    void cleanup(VkDevice device);

    VkSwapchainKHR swapchain() const noexcept { return swapchain_; }

private:
    VkSwapchainKHR swapchain_{ VK_NULL_HANDLE };
};

} // namespace strata::gfx::vk
