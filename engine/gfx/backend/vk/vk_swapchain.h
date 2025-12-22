// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_swapchain.h
//
// Purpose:
//   Declare a Vulkan swapchain RAII wrapper.
// -----------------------------------------------------------------------------

#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "strata/gfx/rhi/gpu_types.h"

namespace strata::gfx::vk {

    // RAII wrapper around a Vulkan swapchain + its image views.
    //
    // This is a backend-only helper, used by VkGpuDevice to own:
    //   - VkSwapchainKHR
    //   - VkImage[]     (non-owning)
    //   - VkImageView[] (owning)
    //
    // It exposes the format, extent, and image/views needed for dynamic rendering.
    class VkSwapchainWrapper {
    public:
        VkSwapchainWrapper() = default;
        ~VkSwapchainWrapper();

        VkSwapchainWrapper(VkSwapchainWrapper&&) noexcept;
        VkSwapchainWrapper& operator=(VkSwapchainWrapper&&) noexcept;

        // Create a swapchain for the given surface + device.
        // Uses the RHI SwapchainDesc (size/format/vsync) and queues to decide sharing mode.
        bool init(VkPhysicalDevice physical,
            VkDevice device,
            VkSurfaceKHR surface,
            std::uint32_t graphics_family,
            std::uint32_t present_family,
            const rhi::SwapchainDesc& desc);

        void cleanup();

        [[nodiscard]] VkSwapchainKHR swapchain() const noexcept { return swapchain_; }
        [[nodiscard]] VkFormat       image_format() const noexcept { return image_format_; }
        [[nodiscard]] VkExtent2D     extent() const noexcept { return extent_; }

        [[nodiscard]] const std::vector<VkImage>& images() const noexcept { return images_; }
        [[nodiscard]] const std::vector<VkImageView>& image_views() const noexcept { return image_views_; }

        [[nodiscard]] bool valid() const noexcept { return device_ != VK_NULL_HANDLE && swapchain_ != VK_NULL_HANDLE; }

    private:
        VkDevice       device_{ VK_NULL_HANDLE };   // non-owning; used for destruction
        VkSwapchainKHR swapchain_{ VK_NULL_HANDLE };

        VkFormat   image_format_{ VK_FORMAT_UNDEFINED };
        VkExtent2D extent_{};

        std::vector<VkImage>     images_;
        std::vector<VkImageView> image_views_;
    };

} // namespace strata::gfx::vk
