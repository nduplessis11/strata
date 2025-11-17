// engine/gfx/include/strata/gfx/vulkan/wsi_bridge.h
#pragma once
#include "strata/platform/wsi_handle.h"
#include <span>

// Forward declare Vulkan types so we don't pull <vulkan/vulkan.h> in public headers
struct VkInstance_T;
struct VkSurfaceKHR_T;
using VkInstance = VkInstance_T*;
using VkSurfaceKHR = VkSurfaceKHR_T*;

namespace strata::gfx::vk {
	// Returns the minimal set of instance extensions required for this WSI
	// e.g., { "VK_KHR_surface", "VK_KHR_win32_surface" } on Win32.
	auto required_instance_extensions(const strata::platform::WsiHandle& wsi) -> std::span<const char* const>;

	// Create a VkSurfaceKHR for the given WSI. Returns VK_NULL_HANDLE on failure.
	// Implemented per-platform in separate .cc files.
	auto create_surface(VkInstance instance, const strata::platform::WsiHandle& wsi) -> VkSurfaceKHR;
} // namespace strata::gfx::vk