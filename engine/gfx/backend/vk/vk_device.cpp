// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_device.cpp
//
// Purpose:
//   Choose a Vulkan physical device and create logical device/queues.
// -----------------------------------------------------------------------------

#include "vk_device.h"
#include "strata/base/diagnostics.h"
#include "vk_check.h"

#include <array>
#include <cstdint>
#include <limits>
#include <print>
#include <span>
#include <string_view>
#include <vector>

namespace strata::gfx::vk
{

namespace
{

using u32                   = std::uint32_t;
constexpr u32 invalid_index = std::numeric_limits<u32>::max();

void log_err(base::Diagnostics* diag, std::string_view msg)
{
    if (!diag)
        return;
    diag->logger().log(base::LogLevel::Error, "vk.device", msg, std::source_location::current());
    diag->debug_break_on_error();
}

void log_warn(base::Diagnostics* diag, std::string_view msg)
{
    if (!diag)
        return;
    diag->logger().log(base::LogLevel::Warn, "vk.device", msg, std::source_location::current());
}

void log_info(base::Diagnostics* diag, std::string_view msg)
{
    if (!diag)
        return;
    diag->logger().log(base::LogLevel::Info, "vk.device", msg, std::source_location::current());
}

[[nodiscard]]
bool vk_ok(base::Diagnostics* diag, VkResult r, char const* what)
{
    if (r == VK_SUCCESS)
        return true;

    log_err(diag, vk_error_message(what, r));
    return false;
}

struct QueueFamilySelection
{
    VkPhysicalDevice physical{VK_NULL_HANDLE};
    u32              graphics_family{invalid_index};
    u32              present_family{invalid_index};

    [[nodiscard]]
    bool complete() const noexcept
    {
        return graphics_family != invalid_index && present_family != invalid_index;
    }
};

[[nodiscard]]
QueueFamilySelection find_queue_families(base::Diagnostics* diag,
                                         VkPhysicalDevice   device,
                                         VkSurfaceKHR       surface)
{
    QueueFamilySelection result{};
    result.physical = device;

    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    if (count == 0)
        return result;

    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (u32 i = 0; i < count; ++i)
    {
        auto const& q = families[i];

        if (q.queueCount > 0 && (q.queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            result.graphics_family = i;
        }

        VkBool32 present_supported = VK_FALSE;
        VkResult pr = vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_supported);
        if (!vk_ok(diag, pr, "vkGetPhysicalDeviceSurfaceSupportKHR"))
        {
            // Can't evaluate presentation support correctly; treat as not supported.
            present_supported = VK_FALSE;
        }

        if (present_supported)
        {
            result.present_family = i;
        }

        if (result.complete())
            break;
    }

    return result;
}

// Keep extensions as C-strings from the start (no string_view/span conversion).
inline constexpr std::array<char const*, 1> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

[[nodiscard]]
bool has_required_device_extensions(base::Diagnostics*           diag,
                                    VkPhysicalDevice             device,
                                    std::span<char const* const> required_exts)
{
    u32      ext_count = 0;
    VkResult r         = vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr);
    if (!vk_ok(diag, r, "vkEnumerateDeviceExtensionProperties(count)"))
        return false;

    if (ext_count == 0)
        return required_exts.empty();

    std::vector<VkExtensionProperties> available(ext_count);
    r = vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, available.data());
    if (!vk_ok(diag, r, "vkEnumerateDeviceExtensionProperties(list)"))
        return false;

    for (char const* req : required_exts)
    {
        bool found = false;
        for (auto const& e : available)
        {
            if (std::string_view{e.extensionName} == req)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    return true;
}

struct Vulkan13Support
{
    VkPhysicalDeviceVulkan13Features f13{
        .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext              = VK_NULL_HANDLE,
        .robustImageAccess  = VK_FALSE,
        .inlineUniformBlock = VK_FALSE,
        .descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE,
        .pipelineCreationCacheControl                       = VK_FALSE,
        .privateData                                        = VK_FALSE,
        .shaderDemoteToHelperInvocation                     = VK_FALSE,
        .shaderTerminateInvocation                          = VK_FALSE,
        .subgroupSizeControl                                = VK_FALSE,
        .computeFullSubgroups                               = VK_FALSE,
        .synchronization2                                   = VK_FALSE,
        .textureCompressionASTC_HDR                         = VK_FALSE,
        .shaderZeroInitializeWorkgroupMemory                = VK_FALSE,
        .dynamicRendering                                   = VK_FALSE,
        .shaderIntegerDotProduct                            = VK_FALSE,
        .maintenance4                                       = VK_FALSE};

    [[nodiscard]]
    bool dynamic_rendering() const noexcept
    {
        return f13.dynamicRendering == VK_TRUE;
    }
    [[nodiscard]]
    bool synchronization2() const noexcept
    {
        return f13.synchronization2 == VK_TRUE;
    }
};

[[nodiscard]]
Vulkan13Support query_vulkan13_support(VkPhysicalDevice physical)
{
    Vulkan13Support out{};

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &out.f13;

    vkGetPhysicalDeviceFeatures2(physical, &features2);
    return out;
}

[[nodiscard]]
QueueFamilySelection pick_physical_device_and_queues(base::Diagnostics*           diag,
                                                     VkInstance                   instance,
                                                     VkSurfaceKHR                 surface,
                                                     std::span<char const* const> required_exts)
{
    u32      count = 0;
    VkResult r     = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (!vk_ok(diag, r, "vkEnumeratePhysicalDevices(count)"))
        return {};

    if (count == 0)
        return {};

    std::vector<VkPhysicalDevice> devices(count);
    r = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (!vk_ok(diag, r, "vkEnumeratePhysicalDevices(list)"))
        return {};

    for (VkPhysicalDevice device : devices)
    {
        QueueFamilySelection q = find_queue_families(diag, device, surface);
        if (!q.complete())
            continue;

        if (!has_required_device_extensions(diag, device, required_exts))
            continue;

        // If you want: prefer discrete GPU here by checking VkPhysicalDeviceProperties.

        return q;
    }

    return {};
}

} // namespace

// --- RAII basics ------------------------------------------------------------

VkDeviceWrapper::~VkDeviceWrapper()
{
    cleanup();
}

VkDeviceWrapper::VkDeviceWrapper(VkDeviceWrapper&& other) noexcept
{
    diagnostics_     = other.diagnostics_;
    device_          = other.device_;
    physical_        = other.physical_;
    graphics_family_ = other.graphics_family_;
    present_family_  = other.present_family_;
    graphics_queue_  = other.graphics_queue_;
    present_queue_   = other.present_queue_;

    other.diagnostics_     = nullptr;
    other.device_          = VK_NULL_HANDLE;
    other.physical_        = VK_NULL_HANDLE;
    other.graphics_family_ = invalid_index;
    other.present_family_  = invalid_index;
    other.graphics_queue_  = VK_NULL_HANDLE;
    other.present_queue_   = VK_NULL_HANDLE;
}

VkDeviceWrapper& VkDeviceWrapper::operator=(VkDeviceWrapper&& other) noexcept
{
    if (this != &other)
    {
        cleanup();

        diagnostics_     = other.diagnostics_;
        device_          = other.device_;
        physical_        = other.physical_;
        graphics_family_ = other.graphics_family_;
        present_family_  = other.present_family_;
        graphics_queue_  = other.graphics_queue_;
        present_queue_   = other.present_queue_;

        other.diagnostics_     = nullptr;
        other.device_          = VK_NULL_HANDLE;
        other.physical_        = VK_NULL_HANDLE;
        other.graphics_family_ = invalid_index;
        other.present_family_  = invalid_index;
        other.graphics_queue_  = VK_NULL_HANDLE;
        other.present_queue_   = VK_NULL_HANDLE;
    }
    return *this;
}

void VkDeviceWrapper::cleanup()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    physical_        = VK_NULL_HANDLE;
    graphics_family_ = invalid_index;
    present_family_  = invalid_index;
    graphics_queue_  = VK_NULL_HANDLE;
    present_queue_   = VK_NULL_HANDLE;
}

bool VkDeviceWrapper::init(VkInstance instance, VkSurfaceKHR surface)
{
    cleanup();

    auto selection =
        pick_physical_device_and_queues(diagnostics_, instance, surface, device_extensions);
    if (!selection.complete())
    {
        log_err(diagnostics_, "VkDeviceWrapper: no suitable physical device found");
        return false;
    }

    // Log device name (helpful for debugging)
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(selection.physical, &props);
    if (diagnostics_)
    {
        STRATA_LOG_INFO(diagnostics_->logger(), "vk.device", "Selected GPU: {}", props.deviceName);
    }

    auto support = query_vulkan13_support(selection.physical);

    if (!support.dynamic_rendering())
    {
        log_err(diagnostics_,
                "Selected physical device does not support Vulkan 1.3 dynamic rendering");
        return false;
    }
    if (!support.synchronization2())
    {
        log_err(diagnostics_,
                "Selected physical device does not support Vulkan 1.3 synchronization2");
        return false;
    }

    // Queues: create one queue per unique family.
    std::vector<VkDeviceQueueCreateInfo> queue_infos;

    std::vector<u32> unique_families;
    unique_families.reserve(2);
    unique_families.push_back(selection.graphics_family);
    if (selection.present_family != selection.graphics_family)
    {
        unique_families.push_back(selection.present_family);
    }

    queue_infos.reserve(unique_families.size());

    float const queue_priority = 1.0f;
    for (u32 const family_index : unique_families)
    {
        VkDeviceQueueCreateInfo qci{};
        qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = family_index;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &queue_priority;
        queue_infos.push_back(qci);
    }

    // Enable Vulkan 1.3 features (copy supported -> enable what you need)
    VkPhysicalDeviceVulkan13Features enabled13{};
    enabled13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabled13.dynamicRendering = VK_TRUE;
    enabled13.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                   = &enabled13;
    dci.queueCreateInfoCount    = static_cast<u32>(queue_infos.size());
    dci.pQueueCreateInfos       = queue_infos.data();
    dci.enabledExtensionCount   = static_cast<u32>(device_extensions.size());
    dci.ppEnabledExtensionNames = device_extensions.data();
    dci.pEnabledFeatures        = nullptr;

    VkResult dres = vkCreateDevice(selection.physical, &dci, nullptr, &device_);
    if (dres != VK_SUCCESS)
    {
        log_err(diagnostics_, vk_error_message("vkCreateDevice", dres));
        cleanup();
        return false;
    }

    physical_        = selection.physical;
    graphics_family_ = selection.graphics_family;
    present_family_  = selection.present_family;

    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);

    log_info(diagnostics_, "Vulkan logical device created");
    return true;
}

} // namespace strata::gfx::vk
