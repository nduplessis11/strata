// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_instance.cpp
//
// Purpose:
//   Create the Vulkan instance, surface, and optional debug messenger.
// -----------------------------------------------------------------------------

#include "vk_instance.h"
#include "vk_wsi_bridge.h"

#include <algorithm>
#include <print>
#include <vector>
#include <vulkan/vulkan.h>

namespace strata::gfx::vk
{
namespace
{

inline constexpr bool kEnableValidation = (STRATA_VK_VALIDATION != 0);

constexpr char const* kValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    [[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT        type,
    VkDebugUtilsMessengerCallbackDataEXT const*             callback_data,
    void* /*user_data*/)
{

    // Filter severity/type here if needed.
    std::println(stderr,
                 "[vk] {}",
                 (callback_data && callback_data->pMessage) ? callback_data->pMessage : "(null)");

    return VK_FALSE;
}

void populate_debug_messenger_ci(
    VkDebugUtilsMessengerCreateInfoEXT& ci)
{
    ci                 = {};
    ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debug_callback;
    ci.pUserData       = nullptr;
}

bool has_layer_support()
{
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> props(count);
    vkEnumerateInstanceLayerProperties(&count, props.data());

    for (char const* want : kValidationLayers)
    {
        bool const found = std::any_of(props.begin(),
                                       props.end(),
                                       [&](VkLayerProperties const& p)
                                       { return std::strcmp(p.layerName, want) == 0; });
        if (!found)
            return false;
    }
    return true;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance                                instance,
    VkDebugUtilsMessengerCreateInfoEXT const* create_info,
    VkAllocationCallbacks const*              allocator,
    VkDebugUtilsMessengerEXT*                 out)
{

    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!fn)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return fn(instance, create_info, allocator, out);
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance                   instance,
    VkDebugUtilsMessengerEXT     messenger,
    VkAllocationCallbacks const* allocator)
{

    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn)
        fn(instance, messenger, allocator);
}

} // namespace

VkInstanceWrapper::~VkInstanceWrapper()
{
    cleanup();
}

VkInstanceWrapper::VkInstanceWrapper(
    VkInstanceWrapper&& other) noexcept
    : instance_(other.instance_),
      surface_(other.surface_),
      debug_messenger_(other.debug_messenger_)
{
    other.instance_        = VK_NULL_HANDLE;
    other.surface_         = VK_NULL_HANDLE;
    other.debug_messenger_ = VK_NULL_HANDLE;
}

VkInstanceWrapper& VkInstanceWrapper::operator=(
    VkInstanceWrapper&& other) noexcept
{
    if (this != &other)
    {
        cleanup();
        instance_        = other.instance_;
        surface_         = other.surface_;
        debug_messenger_ = other.debug_messenger_;

        other.instance_        = VK_NULL_HANDLE;
        other.surface_         = VK_NULL_HANDLE;
        other.debug_messenger_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkInstanceWrapper::init(
    strata::platform::WsiHandle const& wsi)
{
    cleanup();

    // --- Instance extensions from vk_wsi_bridge -------------------------
    auto                     ext_span = required_instance_extensions(wsi);
    std::vector<char const*> exts;
    exts.reserve(ext_span.size());

    for (auto sv : ext_span)
    {
        // vk_wsi_bridge returns string_view; underlying data is static.
        exts.push_back(sv.data());
    }

    bool const want_validation   = kEnableValidation;
    bool const have_validation   = (!want_validation) ? false : has_layer_support();
    bool const enable_validation = want_validation && have_validation;

    if (want_validation && !have_validation)
    {
        std::println(stderr,
                     "[vk] Validation requested, but VK_LAYER_KHRONOS_validation not found. "
                     "Continuing without layers.");
    }

    if (enable_validation)
    {
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // --- Application info -----------------------------------------------
    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "strata";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.pEngineName        = "strata";
    app.engineVersion      = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.apiVersion         = VK_API_VERSION_1_3;

    // Optional: allow validation messages during vkCreateInstance
    VkDebugUtilsMessengerCreateInfoEXT debug_ci{};
    if (enable_validation)
    {
        populate_debug_messenger_ci(debug_ci);
    }

    // --- Instance create info -------------------------------------------
    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = static_cast<std::uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    if (enable_validation)
    {
        ci.enabledLayerCount   = static_cast<std::uint32_t>(std::size(kValidationLayers));
        ci.ppEnabledLayerNames = kValidationLayers;
        ci.pNext               = &debug_ci; // hook messages from instance creation
    }
    else
    {
        ci.enabledLayerCount   = 0;
        ci.ppEnabledLayerNames = nullptr;
        ci.pNext               = nullptr;
    }

    VkResult res = vkCreateInstance(&ci, nullptr, &instance_);
    if (res != VK_SUCCESS)
    {
        std::println(stderr, "vkCreateInstance failed: {}", static_cast<int>(res));
        cleanup();
        return false;
    }

    // Create debug messenger AFTER instance creation
    if (enable_validation)
    {
        res = CreateDebugUtilsMessengerEXT(instance_, &debug_ci, nullptr, &debug_messenger_);
        if (res != VK_SUCCESS)
        {
            std::println(
                stderr,
                "[vk] vkCreateDebugUtilsMessengerEXT failed: {} (continuing without messenger)",
                static_cast<int>(res));
            debug_messenger_ = VK_NULL_HANDLE;
        }
    }

    // --- Surface via vk_wsi_bridge --------------------------------------
    surface_ = create_surface(instance_, wsi);
    if (surface_ == VK_NULL_HANDLE)
    {
        std::println(stderr, "vk_wsi_bridge::create_surface failed");
        cleanup();
        return false;
    }

    return true;
}

void VkInstanceWrapper::cleanup()
{
    if (instance_ != VK_NULL_HANDLE)
    {
        // Destroy messenger first (it references the instance)
        if (debug_messenger_ != VK_NULL_HANDLE)
        {
            DestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
            debug_messenger_ = VK_NULL_HANDLE;
        }

        if (surface_ != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    else
    {
        surface_         = VK_NULL_HANDLE;
        debug_messenger_ = VK_NULL_HANDLE;
    }
}

} // namespace strata::gfx::vk
