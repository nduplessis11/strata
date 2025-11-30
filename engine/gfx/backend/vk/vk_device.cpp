// engine/gfx/backend/vk/vk_device.cpp

#include "vk_device.h"

namespace strata::gfx::vk {

    VkDeviceWrapper::~VkDeviceWrapper() {
        cleanup();
    }

    VkDeviceWrapper::VkDeviceWrapper(VkDeviceWrapper&& other) noexcept {
        device_ = other.device_;
        other.device_ = VK_NULL_HANDLE;
    }

    VkDeviceWrapper&
        VkDeviceWrapper::operator=(VkDeviceWrapper&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    bool VkDeviceWrapper::init(VkInstance) {
        // Stub: real implementation would choose physical device and create VkDevice
        device_ = VK_NULL_HANDLE;
        return true;
    }

    void VkDeviceWrapper::cleanup() {
        // Stub: real implementation would call vkDestroyDevice
        device_ = VK_NULL_HANDLE;
    }

} // namespace strata::gfx::vk
