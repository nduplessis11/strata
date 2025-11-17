#include "strata/gfx/vulkan/wsi_bridge.h"
#include <array>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <windows.h>

using strata::platform::WsiHandle;
namespace wsi = strata::platform::wsi;

namespace {
	constexpr std::array<const char*, 2> kExts{
	  VK_KHR_SURFACE_EXTENSION_NAME,
	  VK_KHR_WIN32_SURFACE_EXTENSION_NAME
	};
} // namespace

namespace strata::gfx::vk {
	std::span<const char* const>
		required_instance_extensions(const WsiHandle&) {
		return std::span{ kExts };
	}

	VkSurfaceKHR create_surface(VkInstance instance, const WsiHandle& h) {
		return std::visit([&](auto const& alt) -> VkSurfaceKHR {
			using T = std::decay_t<decltype(alt)>;
			if constexpr (std::is_same_v<T, wsi::Win32>) {
				VkSurfaceKHR surface = VK_NULL_HANDLE;
				VkWin32SurfaceCreateInfoKHR ci{ };
				ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
				ci.hinstance = reinterpret_cast<HINSTANCE>(alt.instance.value);
				ci.hwnd = reinterpret_cast<HWND>(alt.window.value);
				if (vkCreateWin32SurfaceKHR(instance, &ci, nullptr, &surface) != VK_SUCCESS)
					return VK_NULL_HANDLE;
				return surface;
			}
			else {
				// Wrong WSI passed to Win32 bridge
				return VK_NULL_HANDLE;
			}
			}, h);
	}

} // namespace strata::gfx::vk
