// engine/gfx/backend/vk/vk_swapchain.cpp

#include "vk_swapchain.h"

namespace strata::gfx::vk {

    VkSwapchainWrapper::~VkSwapchainWrapper() = default;

    VkSwapchainWrapper::VkSwapchainWrapper(VkSwapchainWrapper&& other) noexcept {
        swapchain_ = other.swapchain_;
        other.swapchain_ = VK_NULL_HANDLE;
    }

    VkSwapchainWrapper&
        VkSwapchainWrapper::operator=(VkSwapchainWrapper&& other) noexcept {
        if (this != &other) {
            swapchain_ = other.swapchain_;
            other.swapchain_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    bool VkSwapchainWrapper::init(VkDevice, const rhi::SwapchainDesc&) {
        // Stub: real implementation would create VkSwapchainKHR
        swapchain_ = VK_NULL_HANDLE;
        return true;
    }

    void VkSwapchainWrapper::cleanup(VkDevice) {
        // Stub: real implementation would destroy swapchain
        swapchain_ = VK_NULL_HANDLE;
    }

} // namespace strata::gfx::vk
