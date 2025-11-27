// -----------------------------------------------------------------------------
// engine/gfx/src/backends/vulkan/vulkan_context.cc
//
// Purpose:
//   Creates and owns core Vulkan objects required by the graphics backend,
//   including the instance, surface, logical device, and command queues.
// -----------------------------------------------------------------------------

#include "strata/gfx/vulkan/vulkan_context.h"

#include <vulkan/vulkan.h>
#include <print>
#include <vector>
#include <array>
#include <ranges>
#include <algorithm>

namespace strata::gfx {
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

        QueueFamilySelection find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);
        bool check_device_extension_support(VkPhysicalDevice device);

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

            for (u32 i = 0; i < count; i++) {
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

            // If the implementation doesn't know about Vulkan 1.3 features, this will stay VK_FALSE.
            return features13.dynamicRendering == VK_TRUE;
        }

    }

    VulkanContext::InstanceHandle::~InstanceHandle() {
        if (handle_) {
            vkDestroyInstance(handle_, nullptr);
            handle_ = VK_NULL_HANDLE;
        }
    }

    VulkanContext::InstanceHandle::InstanceHandle(InstanceHandle&& other) noexcept
        : handle_(other.handle_) {
        other.handle_ = VK_NULL_HANDLE;
    }

    VulkanContext::InstanceHandle& VulkanContext::InstanceHandle::operator=(InstanceHandle&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                vkDestroyInstance(handle_, nullptr);
            }
            handle_ = other.handle_;
            other.handle_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VulkanContext::SurfaceHandle::~SurfaceHandle() {
        if (instance_ != VK_NULL_HANDLE && handle_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, handle_, nullptr);
            handle_ = VK_NULL_HANDLE;
            instance_ = VK_NULL_HANDLE;
        }
    }

    VulkanContext::SurfaceHandle::SurfaceHandle(SurfaceHandle&& other) noexcept
        : instance_(other.instance_), handle_(other.handle_) {
        other.instance_ = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }

    VulkanContext::SurfaceHandle&
        VulkanContext::SurfaceHandle::operator=(SurfaceHandle&& other) noexcept {
        if (this != &other) {
            if (instance_ != VK_NULL_HANDLE && handle_ != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(instance_, handle_, nullptr);
            }
            instance_ = other.instance_;
            handle_ = other.handle_;
            other.instance_ = VK_NULL_HANDLE;
            other.handle_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VulkanContext::DeviceHandle::~DeviceHandle() {
        if (handle_) {
            vkDestroyDevice(handle_, nullptr);
            handle_ = VK_NULL_HANDLE;
        }
    }

    VulkanContext::DeviceHandle::DeviceHandle(DeviceHandle&& other) noexcept
        : handle_(other.handle_) {
        other.handle_ = VK_NULL_HANDLE;
    }

    VulkanContext::DeviceHandle& VulkanContext::DeviceHandle::operator=(DeviceHandle&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                vkDestroyDevice(handle_, nullptr);
            }
            handle_ = other.handle_;
            other.handle_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VulkanContext VulkanContext::create(const strata::platform::WsiHandle& wsi, const VulkanContextDesc& desc) {
        VulkanContext ctx{};

        // Require WSI instance extensions (Win32: surface + win32_surface)
        auto span_exts{ vk::required_instance_extensions(wsi) };

        // We expose them as std::string_view; Vulkan wants const char* const*.
        // Here all views refer to static string literals (from Vulkan headers),
        // so .data() is null-terminated and has static lifetime.
        std::vector<const char*> exts;
        exts.reserve(span_exts.size());
        for (auto sv : span_exts) {
            exts.push_back(sv.data());
        }

        // Later: if (desc.enable_validation) { add VK_EXT_debug_utils, layers, etc. }
        (void)desc;

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "strata";
        app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app.pEngineName = "strata";
        app.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();

        VkInstance instance = VK_NULL_HANDLE;
        const VkResult res{ vkCreateInstance(&ci, nullptr, &instance) };
        if (res != VK_SUCCESS) {
            std::println(stderr, "VkCreateInstance failed");
            return ctx; // ctx.instance_ stays empty -> valid() == false
        }

        // Wrap in RAII handle; VulkanContext stays Rule of Zero.
        ctx.instance_ = InstanceHandle{ instance };

        VkSurfaceKHR surface{ vk::create_surface(instance, wsi) };
        if (!surface) {
            std::println(stderr, "vk::create_surface failed");
            // Depending on policy:
            // - either leave ctx.surface_ invalid, but ctx.valid() is still true (instance only)
            // - or consider that a fatal error and return an "invalid" context
            return ctx;
        }

        ctx.surface_ = SurfaceHandle{ instance, surface };

        // Pick a physical device + queue families that can present to this surface
        auto selection = pick_physical_device_and_queues(instance, surface);
        if (!selection.complete()) {
            std::println(stderr, "No suitable physical device found.");
            return ctx; // instance + surface valid, but no device
        }

        // Check dynamic rendering support
        if (!supports_dynamic_rendering(selection.physical)) {
            std::println(stderr, "Selected physical device does not support Vulkan 1.3 dynamic rendering.");
            // For now we just fail; later we could fall back to legacy render passes.
            return ctx;
        }

        // Create logical device + queues
        // We need to create infos for the unique families (graphics + present).
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
            qci.pQueuePriorities = &queue_priority; // one queue at priority 1.0
            queue_infos.push_back(qci);
        }

        // Enable swapchain extension (we already checked support in check_device_extension_support)
        auto dev_ext_span{ required_device_extensions() };
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
        dci.pEnabledFeatures = nullptr; // we're using the features13 struct instead

        VkDevice device{ VK_NULL_HANDLE };
        VkResult dres{ vkCreateDevice(selection.physical, &dci, nullptr, &device) };
        if (dres != VK_SUCCESS) {
            std::println(stderr, "vkCreateDevice failed: {}", static_cast<int>(dres));
            return ctx;
        }

        // Wrap device in RAII handle
        ctx.device_ = DeviceHandle{ device };

        // Store non-owning GPU + queue info in the context
        ctx.physical_ = selection.physical;
        ctx.graphics_family_ = selection.graphics_family;
        ctx.present_family_ = selection.present_family;

        vkGetDeviceQueue(device, selection.graphics_family, 0, &ctx.graphics_queue_);
        vkGetDeviceQueue(device, selection.present_family, 0, &ctx.present_queue_);

        return ctx;
    }
}
