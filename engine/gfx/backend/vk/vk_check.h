// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_check.h
//
// Purpose:
//   Backend-only Vulkan result checking helpers.
//   - STRATA_VK_ASSERT: must-succeed (fatal/abort)
//   - STRATA_VK_ASSERT_RETURN: logs + returns a caller-specified value
//
// Notes:
//   - Only include this from gfx/backend/vk/*
// -----------------------------------------------------------------------------

#pragma once

#include <format>
#include <source_location>
#include <string_view>
#include <vulkan/vulkan.h>

#include "strata/base/diagnostics.h"

namespace strata::gfx::vk
{

[[nodiscard]]
constexpr std::string_view to_string(VkResult r) noexcept
{
    switch (r)
    {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:
        return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "VK_RESULT_UNKNOWN";
    }
}

[[nodiscard]]
inline std::string vk_error_message(char const* expr, VkResult r)
{
    return std::format("{} -> {} ({})", expr, to_string(r), static_cast<int>(r));
}

} // namespace strata::gfx::vk

#define STRATA_VK_ASSERT(diag, vk_call)                                                            \
    do                                                                                             \
    {                                                                                              \
        VkResult _strata_vk_result = (vk_call);                                                    \
        if (_strata_vk_result != VK_SUCCESS)                                                       \
        {                                                                                          \
            (diag).fatal("vulkan",                                                                 \
                         ::strata::gfx::vk::vk_error_message(#vk_call, _strata_vk_result),         \
                         std::source_location::current());                                         \
        }                                                                                          \
    } while (0)

#define STRATA_VK_ASSERT_RETURN(diag, vk_call, return_value)                                       \
    do                                                                                             \
    {                                                                                              \
        VkResult _strata_vk_result = (vk_call);                                                    \
        if (_strata_vk_result != VK_SUCCESS)                                                       \
        {                                                                                          \
            (diag).logger().log(::strata::base::LogLevel::Error,                                   \
                                "vulkan",                                                          \
                                ::strata::gfx::vk::vk_error_message(#vk_call, _strata_vk_result),  \
                                std::source_location::current());                                  \
            (diag).debug_break_on_error(std::source_location::current());                          \
            return (return_value);                                                                 \
        }                                                                                          \
    } while (0)
