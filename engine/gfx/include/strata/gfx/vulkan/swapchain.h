// engine/gfx/include/strata/gfx/vulkan/swapchain.h

#pragma once

#include "vulkan_context.h"
#include "strata/platform/window.h"

#include <vector>

struct VkSwapchainKHR_T;
struct VkImageView_T;

using VkSwapchainKHR = VkSwapchainKHR_T*;
using VkImageView = VkImageView_T*;

namespace strata::gfx {
    using strata::platform::Extent2d;

    class Swapchain {
    public:
        [[nodiscard]] static Swapchain create(const VulkanContext& ctx,
            Extent2d window_size);

        // Maybe add lightweight accessors later, e.g.:
        // [[nodiscard]] Extent2d extent() const noexcept { return extent_; }
        // [[nodiscard]] std::span<const VkImageView> image_views() const noexcept { return handle_.views(); }

    private:
        // Rule of Zero: no user-declared dtor/ctor/move/copy.
        Swapchain() = default;

        // Small RAII type that owns the Vulkan swapchain + its image views.
        struct Handle {
            Handle() = default;
            Handle(VkDevice device,
                VkSwapchainKHR swapchain,
                std::vector<VkImageView> views);

            ~Handle();

            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;

            Handle(Handle&& other) noexcept;
            Handle& operator=(Handle&& other) noexcept;

            [[nodiscard]] bool valid() const noexcept { return device_ != nullptr && swapchain_ != nullptr; }
            [[nodiscard]] VkSwapchainKHR get() const noexcept { return swapchain_; }
            [[nodiscard]] VkDevice       device() const noexcept { return device_; }
            [[nodiscard]] const std::vector<VkImageView>& views() const noexcept { return image_views_; }

        private:
            VkDevice       device_{ nullptr };      // non-owning: used for destruction
            VkSwapchainKHR swapchain_{ nullptr };   // owning
            std::vector<VkImageView> image_views_;  // owning
        };

        Handle  handle_{};  // RAII; Swapchain doesn't need its own destructor
        Extent2d extent_{}; // engine-side representation of size
    };
} // namespace strata::gfx
