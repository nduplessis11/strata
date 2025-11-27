// -----------------------------------------------------------------------------
// engine/gfx/src/backends/vulkan/wsi_bridge_x11.cc
//
// Purpose:
//   X11-specific implementation of the Vulkan WSI bridge. Supplies the
//   necessary instance extensions and creates VkSurfaceKHR objects for Xlib
//   windows.
// -----------------------------------------------------------------------------

#include "strata/gfx/vulkan/wsi_bridge.h"
#include <array>
#include <type_traits>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>
#include <X11/Xlib.h>

using strata::platform::WsiHandle;
namespace wsi = strata::platform::wsi;

namespace {
    static constexpr std::array<std::string_view, 2> kExtViews = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME
    };
}

namespace strata::gfx::vk {
    std::span<const ExtensionName> required_instance_extensions(const WsiHandle&) {
        return std::span{ kExtViews };
    }

    VkSurfaceKHR create_surface(VkInstance instance, const WsiHandle& h) {
        return std::visit([&](auto const& alt) -> VkSurfaceKHR {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, wsi::X11>) {
                VkSurfaceKHR surface = VK_NULL_HANDLE;

                VkXlibSurfaceCreateInfoKHR ci{};
                ci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
                ci.dpy = reinterpret_cast<Display*>(alt.display.value);
                ci.window = static_cast<::Window>(alt.window.value);

                if (vkCreateXlibSurfaceKHR(instance, &ci, nullptr, &surface) != VK_SUCCESS) {
                    return VK_NULL_HANDLE;
                }
                return surface;
            }
            else {
                return VK_NULL_HANDLE;
            }
        }, h);
    }
} // namespace strata::gfx::vk
