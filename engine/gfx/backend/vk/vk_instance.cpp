// engine/gfx/backend/vk/vk_instance.cpp

#include "vk_instance.h"

namespace strata::gfx::vk {

    VkInstanceWrapper::~VkInstanceWrapper() {
        cleanup();
    }

    VkInstanceWrapper::VkInstanceWrapper(VkInstanceWrapper&& other) noexcept {
        instance_ = other.instance_;
        surface_ = other.surface_;
        other.instance_ = VK_NULL_HANDLE;
        other.surface_ = VK_NULL_HANDLE;
    }

    VkInstanceWrapper&
        VkInstanceWrapper::operator=(VkInstanceWrapper&& other) noexcept {
        if (this != &other) {
            cleanup();
            instance_ = other.instance_;
            surface_ = other.surface_;
            other.instance_ = VK_NULL_HANDLE;
            other.surface_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    bool VkInstanceWrapper::init(const strata::platform::WsiHandle&) {
        // Stubbed: real implementation would create instance + surface using WSI handle
        instance_ = VK_NULL_HANDLE;
        surface_ = VK_NULL_HANDLE;
        return true;
    }

    void VkInstanceWrapper::cleanup() {
        // Stub for now; real code would destroy surface + instance via vkDestroy* calls
        instance_ = VK_NULL_HANDLE;
        surface_ = VK_NULL_HANDLE;
    }

} // namespace strata::gfx::vk
