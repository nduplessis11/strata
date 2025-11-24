// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/vulkan/swapchain.h
//
// Purpose:
//   RAII wrapper around a Vulkan swapchain and its image views. This header
//   provides a small, engine-facing interface for creating a window-sized
//   swapchain from an existing VulkanContext, without exposing the details of
//   swapchain creation (formats, present modes, capabilities, etc.) to callers.
//
//   The swapchain owns:
//     • VkSwapchainKHR  – the WSI-managed series of presentable images
//     • VkImageView[]   – one view per swapchain image, used as color attachments
//
//   VulkanContext owns:
//     • VkInstance, VkSurfaceKHR, VkDevice, VkPhysicalDevice, queues
//   Swapchain::create() uses these to construct a swapchain suitable for
//   rendering to a given window/Extent2d.
//
// Design Notes:
//   • Public Header: We avoid including <vulkan/vulkan.h> here. Instead we
//     forward-declare Vulkan handle types (VkSwapchainKHR, VkImageView) and
//     reuse VkDevice from vulkan_context.h. The implementation file
//     (swapchain.cc) includes the real Vulkan headers.
//
//   • Rule of Zero:
//       - Swapchain itself declares no destructor, copy, or move operations.
//       - All Vulkan lifetime management lives in the nested Handle struct
//         (which is move-only and RAII).
//       - Swapchain simply aggregates Handle and an Extent2d.
//
//   • Ownership Model:
//       - Handle owns the swapchain (VkSwapchainKHR) and the image views
//         (std::vector<VkImageView>).
//       - Handle stores a non-owning VkDevice pointer, used only to destroy
//         the views and swapchain; VulkanContext remains the true owner of
//         the device.
//       - Destroy order in Handle::~Handle():
//           1) Destroy all VkImageView objects
//           2) Destroy the VkSwapchainKHR
//
//   • Separation of Concerns:
//        Header  → declares the high-level Swapchain interface and RAII shape.
//        Source  → implements Vulkan-specific logic:
//                    - querying surface capabilities,
//                    - choosing formats/present modes/extents,
//                    - creating the swapchain and image views,
//                    - destruction details.
// -----------------------------------------------------------------------------


#pragma once

#include "vulkan_context.h"
#include "strata/platform/window.h"

#include <vector>
#include <span>

struct VkSwapchainKHR_T;
struct VkImageView_T;
struct VkImage_T;

using VkSwapchainKHR = VkSwapchainKHR_T*;
using VkImageView = VkImageView_T*;
using VkImage = VkImage_T*;

namespace strata::gfx {
    using strata::platform::Extent2d;

    class Swapchain {
    public:
        [[nodiscard]] static Swapchain create(const VulkanContext& ctx, Extent2d window_size);

        [[nodiscard]] Extent2d extent() const noexcept { return extent_; }
        [[nodiscard]] std::span<const VkImageView> image_views() const noexcept { return handle_.views(); }
        [[nodiscard]] std::span<const VkImage> images() const noexcept { return handle_.images(); }

        [[nodiscard]] VkSwapchainKHR handle() const noexcept { return handle_.get(); }
        [[nodiscard]] bool valid() const noexcept { return handle_.valid(); }

    private:
        // Rule of Zero: no user-declared dtor/ctor/move/copy.
        Swapchain() = default;

        // Small RAII type that owns the Vulkan swapchain + its image views.
        struct Handle {
            Handle() = default;
            Handle(VkDevice device, VkSwapchainKHR swapchain, std::vector<VkImage> images, std::vector<VkImageView> views);

            ~Handle();

            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;

            Handle(Handle&& other) noexcept;
            Handle& operator=(Handle&& other) noexcept;

            [[nodiscard]] bool                            valid()  const noexcept { return device_ != nullptr && swapchain_ != nullptr; }
            [[nodiscard]] VkSwapchainKHR                  get()    const noexcept { return swapchain_; }
            [[nodiscard]] VkDevice                        device() const noexcept { return device_; }
            [[nodiscard]] const std::vector<VkImageView>& views()  const noexcept { return image_views_; }
            [[nodiscard]] const std::vector<VkImage>&     images() const noexcept { return images_; }

        private:
            VkDevice       device_{ nullptr };      // non-owning: used for destruction
            VkSwapchainKHR swapchain_{ nullptr };   // owning
            std::vector<VkImage> images_;           // non-owning handles (owned by swapchain)
            std::vector<VkImageView> image_views_;  // owning (we create/destroy)
        };

        Handle  handle_{};  // RAII; Swapchain doesn't need its own destructor
        Extent2d extent_{}; // engine-side representation of size
    };
} // namespace strata::gfx
