// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_wsi_bridge_win32.cpp
//
// Purpose:
//   Win32-specific implementation of the Vulkan WSI bridge. This file provides:
//      1. The list of Vulkan instance extensions required on Win32.
//      2. A function for creating a VkSurfaceKHR from a Win32 window (HWND).
//
// Design Notes:
//   • Static Extension List:
//       Uses constexpr std::array<const char*, 2> to store the Win32-required
//       instance extensions (VK_KHR_surface, VK_KHR_win32_surface). This array is
//       compile-time constant and has static storage duration, allowing us to
//       return a std::span pointing to it safely.
//
//   • Why std::array?
//       - No dynamic allocation.
//       - Size is part of the type (stronger correctness).
//       - Works naturally with std::span.
//
//   • Why std::span Return Type?
//       required_instance_extensions() returns std::span<const ExtensionName>,
//       exposing a read-only std::string_view to the static extension list without
//       copying or allocating memory. The lifetime is guaranteed because the array
//       is static.
//
//   • Variant Dispatch (std::visit):
//       WsiHandle is a std::variant<Win32, X11, Wayland>. We use std::visit with
//       an if constexpr type check to select the correct Vulkan surface creation
//       path at compile time. This avoids virtual functions or runtime branching.
//
//       If the WsiHandle does not contain wsi::Win32, this implementation reports
//       an error by returning VK_NULL_HANDLE.
//
//   • Header Hygiene:
//       This .cc file includes the real Vulkan headers and Win32 API headers,
//       keeping them out of the public interface. This reduces compile-time
//       dependencies and isolates platform-specific detail.
//
// -----------------------------------------------------------------------------
#include "vk_wsi_bridge.h"
#include <array>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <windows.h>

using strata::platform::WsiHandle;
namespace wsi = strata::platform::wsi;

namespace
{
static constexpr std::array<std::string_view, 2> kExtViews = {VK_KHR_SURFACE_EXTENSION_NAME,
                                                              VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
} // namespace

namespace strata::gfx::vk
{
std::span<ExtensionName const> required_instance_extensions(
    WsiHandle const&)
{
    return std::span{kExtViews};
}

VkSurfaceKHR create_surface(
    VkInstance       instance,
    WsiHandle const& h)
{
    return std::visit(
        [&](auto const& alt) -> VkSurfaceKHR
        {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, wsi::Win32>)
            {
                VkSurfaceKHR surface = VK_NULL_HANDLE;

                VkWin32SurfaceCreateInfoKHR ci{};
                ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                ci.hinstance = reinterpret_cast<HINSTANCE>(alt.instance.value);
                ci.hwnd      = reinterpret_cast<HWND>(alt.window.value);

                if (vkCreateWin32SurfaceKHR(instance, &ci, nullptr, &surface) != VK_SUCCESS)
                {
                    return VK_NULL_HANDLE;
                }
                return surface;
            }
            else
            {
                // Wrong WSI passed to Win32 bridge
                return VK_NULL_HANDLE;
            }
        },
        h);
}

} // namespace strata::gfx::vk
