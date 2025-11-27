// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/vulkan/wsi_bridge.h
// 
// Purpose:
//   Cross-platform bridge between the engine's platform layer (Win32/X11/Wayland)
//   and Vulkan’s Window System Integration (WSI). This header exposes a minimal,
//   platform-agnostic interface for:
//      1. Querying the Vulkan instance extensions required by the active WSI.
//      2. Creating a VkSurfaceKHR from the engine's window handle.
//
// Design Notes:
//   • Public Header: We avoid including <vulkan/vulkan.h> here because that would
//     force all translation units that depend on the graphics subsystem to pull in
//     heavy Vulkan headers. Instead, we forward-declare Vulkan handle types.
//     Vulkan handles are opaque pointers, so forward declarations are sufficient.
//
//   • Data Model: WsiHandle is a std::variant of platform-specific structs
//     (Win32/X11/Wayland). This lets the API remain platform-neutral while
//     implementation happens in platform-specific .cc files via std::visit.
//
//   • Return Type: required_instance_extensions() returns std::span<const char* const>.
//     - span = non-owning view → zero allocations, lightweight API.
//     - pointer + length encoded cleanly in one type.
//     - const char* const prevents callers from modifying strings or pointer storage.
//     Platforms expose static constexpr extension arrays which span can reference.
//
//   • Separation of Concerns:
//        Header → declares the interface, platform-agnostic.
//        .cc file (per platform) → contains implementation and includes real Vulkan headers.
//
// -----------------------------------------------------------------------------
#pragma once
#include "strata/platform/wsi_handle.h"
#include <string_view>
#include <span>

// Forward declare Vulkan types so we don't pull <vulkan/vulkan.h> in public headers
struct VkInstance_T;
struct VkSurfaceKHR_T;
using VkInstance = VkInstance_T*;
using VkSurfaceKHR = VkSurfaceKHR_T*;

namespace strata::gfx::vk {
    // A lightweight, read-only, non-owning view of a list of extension names.
    using ExtensionName = std::string_view;

    // Returns the minimal set of instance extensions required for this WSI
    // e.g., { "VK_KHR_surface", "VK_KHR_win32_surface" } on Win32.
    auto required_instance_extensions(const strata::platform::WsiHandle& wsi) -> std::span<const ExtensionName>;

    // Create a VkSurfaceKHR for the given WSI. Returns VK_NULL_HANDLE on failure.
    // Implemented per-platform in separate .cc files.
    auto create_surface(VkInstance instance, const strata::platform::WsiHandle& wsi) -> VkSurfaceKHR;
} // namespace strata::gfx::vk