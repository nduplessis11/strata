// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_instance.cpp
//
// Purpose:
//   Create the Vulkan instance, surface, and optional debug messenger.
// -----------------------------------------------------------------------------

#include "vk_instance.h"
#include "vk_check.h"
#include "vk_wsi_bridge.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "strata/base/diagnostics.h"

namespace strata::gfx::vk
{
namespace
{

inline constexpr bool vk_validation_requested = (STRATA_VK_VALIDATION != 0);
constexpr char const* validation_layers[]     = {"VK_LAYER_KHRONOS_validation"};

base::LogLevel map_vk_severity(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        return base::LogLevel::Error;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        return base::LogLevel::Warn;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        return base::LogLevel::Info;
    return base::LogLevel::Debug; // VERBOSE -> Debug (avoid Trace spam by default)
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT severity,
               [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT        type,
               VkDebugUtilsMessengerCallbackDataEXT const*             callback_data,
               void*                                                   user_data)
{
    auto* diag = static_cast<base::Diagnostics*>(user_data);
    if (!diag)
        return VK_FALSE;

    std::string_view const msg =
        (callback_data && callback_data->pMessage) ? callback_data->pMessage : "(null)";

    // Pass default source_location (line==0) so sinks can omit useless file:line.
    diag->logger().log(map_vk_severity(severity), "vk.validation", msg, std::source_location{});
    return VK_FALSE;
}

void populate_debug_messenger_ci(VkDebugUtilsMessengerCreateInfoEXT& ci, base::Diagnostics* diag)
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
    ci.pUserData       = diag;
}

bool has_layer_support(base::Diagnostics& diagnostics)
{
    std::uint32_t count = 0;
    STRATA_VK_ASSERT_RETURN(diagnostics,
                            vkEnumerateInstanceLayerProperties(&count, nullptr),
                            false);

    std::vector<VkLayerProperties> props(count);
    STRATA_VK_ASSERT_RETURN(diagnostics,
                            vkEnumerateInstanceLayerProperties(&count, props.data()),
                            false);

    for (char const* want : validation_layers)
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

VkResult create_debug_utils_messenger_ext(VkInstance                                instance,
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

void destroy_debug_utils_messenger_ext(VkInstance                   instance,
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

VkInstanceWrapper::VkInstanceWrapper(VkInstanceWrapper&& other) noexcept
      : diagnostics_(other.diagnostics_)
      , instance_(other.instance_)
      , surface_(other.surface_)
      , debug_messenger_(other.debug_messenger_)
{
    other.diagnostics_     = nullptr;
    other.instance_        = VK_NULL_HANDLE;
    other.surface_         = VK_NULL_HANDLE;
    other.debug_messenger_ = VK_NULL_HANDLE;
}

VkInstanceWrapper& VkInstanceWrapper::operator=(VkInstanceWrapper&& other) noexcept
{
    if (this != &other)
    {
        cleanup();

        diagnostics_     = other.diagnostics_;
        instance_        = other.instance_;
        surface_         = other.surface_;
        debug_messenger_ = other.debug_messenger_;

        other.diagnostics_     = nullptr;
        other.instance_        = VK_NULL_HANDLE;
        other.surface_         = VK_NULL_HANDLE;
        other.debug_messenger_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkInstanceWrapper::init(base::Diagnostics& diagnostics, strata::platform::WsiHandle const& wsi)
{
    cleanup();
    diagnostics_ = &diagnostics;

    // --- Instance extensions from vk_wsi_bridge -------------------------
    auto                     ext_span = required_instance_extensions(wsi);
    std::vector<char const*> exts;
    exts.reserve(ext_span.size());

    for (auto sv : ext_span)
    {
        // vk_wsi_bridge returns string_view; underlying data is static.
        exts.push_back(sv.data());
    }

    bool const want_validation    = vk_validation_requested;
    bool const have_validation    = want_validation && has_layer_support(diagnostics);
    bool const validation_enabled = want_validation && have_validation;

    VkDebugUtilsMessengerCreateInfoEXT debug_ci{};
    if (want_validation && !have_validation)
    {
        STRATA_LOG_WARN(diagnostics.logger(),
                        "vk",
                        "Validation requested but VK_LAYER_KHRONOS_validation not found; "
                        "continuing without layers.");
    }

    if (validation_enabled)
    {
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        populate_debug_messenger_ci(debug_ci, diagnostics_);
    }

    // --- Application info -----------------------------------------------
    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "strata";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.pEngineName        = "strata";
    app.engineVersion      = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.apiVersion         = VK_API_VERSION_1_3;

    // --- Instance create info -------------------------------------------
    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = static_cast<std::uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    if (validation_enabled)
    {
        ci.enabledLayerCount   = static_cast<std::uint32_t>(std::size(validation_layers));
        ci.ppEnabledLayerNames = validation_layers;
        ci.pNext               = &debug_ci; // enable messages during vkCreateInstance
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
        diagnostics.logger().log(base::LogLevel::Error,
                                 "vk",
                                 vk_error_message("vkCreateInstance", res),
                                 std::source_location{});
        cleanup();
        return false;
    }

    // Create debug messenger AFTER instance creation
    if (validation_enabled)
    {
        res = create_debug_utils_messenger_ext(instance_, &debug_ci, nullptr, &debug_messenger_);
        if (res != VK_SUCCESS)
        {
            STRATA_LOG_WARN(
                diagnostics.logger(),
                "vk",
                "vkCreateDebugUtilsMessengerEXT failed: {} (continuing without messenger)",
                static_cast<int>(res));
            debug_messenger_ = VK_NULL_HANDLE;
        }
        else
        {
            STRATA_LOG_INFO(diagnostics.logger(), "vk", "Vulkan validation messenger enabled");
        }
    }

    // --- Surface via vk_wsi_bridge --------------------------------------
    surface_ = create_surface(instance_, wsi);
    if (surface_ == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "vk", "vk_wsi_bridge::create_surface failed");
        cleanup();
        return false;
    }

    STRATA_LOG_INFO(diagnostics.logger(), "vk", "Vulkan instance + surface created");
    return true;
}

void VkInstanceWrapper::cleanup()
{
    if (instance_ != VK_NULL_HANDLE)
    {
        // Destroy messenger first (it references the instance)
        if (debug_messenger_ != VK_NULL_HANDLE)
        {
            destroy_debug_utils_messenger_ext(instance_, debug_messenger_, nullptr);
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
