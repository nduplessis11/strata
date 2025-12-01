// engine/gfx/backend/vk/vk_instance.cpp

#include "vk_instance.h"
#include "vk_wsi_bridge.h"

#include <vulkan/vulkan.h>
#include <print>
#include <vector>

namespace strata::gfx::vk {

    VkInstanceWrapper::~VkInstanceWrapper() {
        cleanup();
    }

    VkInstanceWrapper::VkInstanceWrapper(VkInstanceWrapper&& other) noexcept
        : instance_(other.instance_)
        , surface_(other.surface_) {
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

    bool VkInstanceWrapper::init(const strata::platform::WsiHandle& wsi) {
        cleanup();

        // --- Instance extensions from vk_wsi_bridge -------------------------
        auto ext_span = required_instance_extensions(wsi);
        std::vector<const char*> exts;
        exts.reserve(ext_span.size());
        for (auto sv : ext_span) {
            // vk_wsi_bridge returns string_view; underlying data is static.
            exts.push_back(sv.data());
        }

        // --- Application info -----------------------------------------------
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "strata";
        app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app.pEngineName = "strata";
        app.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_3;

        // --- Instance create info -------------------------------------------
        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();
        ci.enabledLayerCount = 0;
        ci.ppEnabledLayerNames = nullptr;

        VkResult res = vkCreateInstance(&ci, nullptr, &instance_);
        if (res != VK_SUCCESS) {
            std::println(stderr, "vkCreateInstance failed: {}", static_cast<int>(res));
            cleanup();
            return false;
        }

        // --- Surface via vk_wsi_bridge --------------------------------------
        surface_ = create_surface(instance_, wsi);
        if (surface_ == VK_NULL_HANDLE) {
            std::println(stderr, "vk_wsi_bridge::create_surface failed");
            cleanup();
            return false;
        }

        return true;
    }

    void VkInstanceWrapper::cleanup() {
        if (instance_ != VK_NULL_HANDLE) {
            if (surface_ != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(instance_, surface_, nullptr);
                surface_ = VK_NULL_HANDLE;
            }
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
        else {
            surface_ = VK_NULL_HANDLE;
        }
    }

} // namespace strata::gfx::vk
