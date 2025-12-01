// engine/gfx/backend/vk/vk_device.cpp

#include "vk_device.h"

#include <vulkan/vulkan.h>
#include <print>
#include <vector>
#include <array>
#include <string_view>
#include <span>
#include <ranges>
#include <algorithm>

namespace strata::gfx::vk {
    namespace {

        using u32 = std::uint32_t;
        constexpr u32 kInvalidIndex = std::numeric_limits<u32>::max();

        struct QueueFamilySelection {
            VkPhysicalDevice physical{ VK_NULL_HANDLE };
            u32 graphics_family = kInvalidIndex;
            u32 present_family = kInvalidIndex;

            bool complete() const noexcept {
                return graphics_family != kInvalidIndex && present_family != kInvalidIndex;
            }
        };

        QueueFamilySelection find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
            QueueFamilySelection result{};
            result.physical = device;

            u32 count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            if (count == 0) {
                return result;
            }

            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

            for (u32 i = 0; i < count; ++i) {
                const auto& q = families[i];

                // Graphics support
                if (q.queueCount > 0 && (q.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    result.graphics_family = i;
                }

                // Present support for this surface
                VkBool32 present_supported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_supported);
                if (present_supported == VK_TRUE) {
                    result.present_family = i;
                }

                if (result.complete()) {
                    break;
                }
            }

            return result;
        }

        using DeviceExtName = std::string_view;

        constexpr std::array kDeviceExtensions{
            DeviceExtName{VK_KHR_SWAPCHAIN_EXTENSION_NAME}
            // add more device extensions here if needed
        };

        std::span<const DeviceExtName> required_device_extensions() noexcept {
            return std::span{ kDeviceExtensions };
        }

        bool check_device_extension_support(VkPhysicalDevice device) {
            u32 ext_count = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr);
            if (ext_count == 0) return false;

            std::vector<VkExtensionProperties> available(ext_count);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, available.data());

            std::vector<std::string_view> available_names;
            available_names.reserve(available.size());
            for (auto const& e : available) {
                available_names.emplace_back(e.extensionName);
            }

            auto required = required_device_extensions();

            return std::ranges::all_of(required, [&](std::string_view req) {
                return std::ranges::contains(available_names, req);
                });
        }

        bool supports_dynamic_rendering(VkPhysicalDevice physical) {
            // Query features via the "features2" path.
            VkPhysicalDeviceVulkan13Features features13{};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

            VkPhysicalDeviceFeatures2 features2{};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.pNext = &features13;

            vkGetPhysicalDeviceFeatures2(physical, &features2);

            // If the implementation doesn't know about Vulkan 1.3 features, this stays VK_FALSE.
            return features13.dynamicRendering == VK_TRUE;
        }

        QueueFamilySelection pick_physical_device_and_queues(VkInstance instance, VkSurfaceKHR surface) {
            QueueFamilySelection result{};

            // 1) Enumerate physical devices
            u32 count = 0;
            vkEnumeratePhysicalDevices(instance, &count, nullptr);
            if (count == 0) {
                return result;
            }

            std::vector<VkPhysicalDevice> devices(count);
            vkEnumeratePhysicalDevices(instance, &count, devices.data());

            // 2) Loop over devices and select the first "suitable" one
            for (VkPhysicalDevice device : devices) {
                // a) Queues that can do graphics + present
                QueueFamilySelection q = find_queue_families(device, surface);
                if (!q.complete()) {
                    continue;
                }

                // b) Device extensions (must support swapchain)
                if (!check_device_extension_support(device)) {
                    continue;
                }

                // Maybe also inspect VkPhysicalDeviceProperties to prefer discrete GPUs.

                return q; // first suitable device
            }

            return {}; // no suitable device found
        }

    } // anonymous namespace

    // --- RAII basics ------------------------------------------------------------

    VkDeviceWrapper::~VkDeviceWrapper() {
        cleanup();
    }

    VkDeviceWrapper::VkDeviceWrapper(VkDeviceWrapper&& other) noexcept {
        device_ = other.device_;
        physical_ = other.physical_;
        graphics_family_ = other.graphics_family_;
        present_family_ = other.present_family_;
        graphics_queue_ = other.graphics_queue_;
        present_queue_ = other.present_queue_;

        other.device_ = VK_NULL_HANDLE;
        other.physical_ = VK_NULL_HANDLE;
        other.graphics_family_ = kInvalidIndex;
        other.present_family_ = kInvalidIndex;
        other.graphics_queue_ = VK_NULL_HANDLE;
        other.present_queue_ = VK_NULL_HANDLE;
    }

    VkDeviceWrapper&
        VkDeviceWrapper::operator=(VkDeviceWrapper&& other) noexcept {
        if (this != &other) {
            cleanup();

            device_ = other.device_;
            physical_ = other.physical_;
            graphics_family_ = other.graphics_family_;
            present_family_ = other.present_family_;
            graphics_queue_ = other.graphics_queue_;
            present_queue_ = other.present_queue_;

            other.device_ = VK_NULL_HANDLE;
            other.physical_ = VK_NULL_HANDLE;
            other.graphics_family_ = kInvalidIndex;
            other.present_family_ = kInvalidIndex;
            other.graphics_queue_ = VK_NULL_HANDLE;
            other.present_queue_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void VkDeviceWrapper::cleanup() {
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }

        physical_ = VK_NULL_HANDLE;
        graphics_family_ = kInvalidIndex;
        present_family_ = kInvalidIndex;
        graphics_queue_ = VK_NULL_HANDLE;
        present_queue_ = VK_NULL_HANDLE;
    }

    // --- init(): pick physical device, create logical device + queues ----------

    bool VkDeviceWrapper::init(VkInstance instance, VkSurfaceKHR surface) {
        cleanup();

        auto selection = pick_physical_device_and_queues(instance, surface);
        if (!selection.complete()) {
            std::println(stderr, "VkDeviceWrapper: no suitable physical device found.");
            return false;
        }

        if (!supports_dynamic_rendering(selection.physical)) {
            std::println(stderr, "Selected physical device does not support Vulkan 1.3 dynamic rendering.");
            return false;
        }

        // Create logical device + queues
        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        std::vector<u32> unique_families;
        unique_families.push_back(selection.graphics_family);
        if (selection.present_family != selection.graphics_family) {
            unique_families.push_back(selection.present_family);
        }

        float queue_priority{ 1.0f };
        for (u32 family_index : unique_families) {
            VkDeviceQueueCreateInfo qci{};
            qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qci.queueFamilyIndex = family_index;
            qci.queueCount = 1;
            qci.pQueuePriorities = &queue_priority;
            queue_infos.push_back(qci);
        }

        auto dev_ext_span = required_device_extensions();
        std::vector<const char*> dev_ext_cstrs;
        dev_ext_cstrs.reserve(dev_ext_span.size());
        for (std::string_view sv : dev_ext_span) {
            dev_ext_cstrs.push_back(sv.data());
        }

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.pNext = &features13;
        dci.queueCreateInfoCount = static_cast<u32>(queue_infos.size());
        dci.pQueueCreateInfos = queue_infos.data();
        dci.enabledExtensionCount = static_cast<u32>(dev_ext_cstrs.size());
        dci.ppEnabledExtensionNames = dev_ext_cstrs.data();
        dci.pEnabledFeatures = nullptr; // we rely on features13 instead

        VkResult dres = vkCreateDevice(selection.physical, &dci, nullptr, &device_);
        if (dres != VK_SUCCESS) {
            std::println(stderr, "vkCreateDevice failed: {}", static_cast<int>(dres));
            cleanup();
            return false;
        }

        // Store non-owning GPU + queue info
        physical_ = selection.physical;
        graphics_family_ = selection.graphics_family;
        present_family_ = selection.present_family;

        vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
        vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);

        return true;
    }

} // namespace strata::gfx::vk
