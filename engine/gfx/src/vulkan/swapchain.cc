// engine/gfx/src/vulkan/swapchain.cc
#include "strata/gfx/vulkan/swapchain.h"
#include <vulkan/vulkan.h>

namespace strata::gfx {

    Swapchain::~Swapchain() {
        if (device_ && swapchain_) {
            // destroy image views first
            for (VkImageView view : image_views_) {
                if (view) {
                    vkDestroyImageView(device_, view, nullptr);
                }
            }
            image_views_.clear();

            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = nullptr;
        }
    }

    Swapchain::Swapchain(Swapchain&& other) noexcept
        : device_(other.device_)
        , swapchain_(other.swapchain_)
        , extent_(other.extent_)
        , image_views_(std::move(other.image_views_)) {
        other.device_ = nullptr;
        other.swapchain_ = nullptr;
        other.extent_ = {};
        // moved-from image_views_ is already empty/moved
    }

    Swapchain& Swapchain::operator=(Swapchain&& other) noexcept {
        if (this != &other) {
            // Destroy our current resources
            if (device_ && swapchain_) {
                for (VkImageView view : image_views_) {
                    if (view) {
                        vkDestroyImageView(device_, view, nullptr);
                    }
                }
                image_views_.clear();
                vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            }

            // Steal other's resources
            device_ = other.device_;
            swapchain_ = other.swapchain_;
            extent_ = other.extent_;
            image_views_ = std::move(other.image_views_);

            // Reset other
            other.device_ = nullptr;
            other.swapchain_ = nullptr;
            other.extent_ = {};
        }
        return *this;
    }

    // Swapchain::create(...) will:
    //   - query surface capabilities via ctx.physical_ + ctx.surface()
    //   - pick format + present mode
    //   - choose extent based on window_size
    //   - call vkCreateSwapchainKHR
    //   - get swapchain images + create image views
    //   - fill device_, swapchain_, extent_, image_views_
    //   - return the fully-initialized Swapchain by value
    Swapchain Swapchain::create(const VulkanContext& ctx, Extent2d window_size) {
        Swapchain sc{};
        // ... setup using ctx.device(), ctx.surface(), ctx.physical_ (once exposed) ...
        sc.device_ = ctx.device();
        sc.extent_ = window_size;
        return sc;
    }

} // namespace strata::gfx
