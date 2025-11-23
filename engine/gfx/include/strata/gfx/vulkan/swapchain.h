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
		~Swapchain();

		Swapchain(const Swapchain&) = delete;
		Swapchain& operator=(const Swapchain&) = delete;

		Swapchain(Swapchain&&) noexcept;
		Swapchain& operator=(Swapchain&&) noexcept;

		[[nodiscard]] static Swapchain create(const VulkanContext& ctx, Extent2d window_size);
		// acquire_next_image, present, recreate, etc.
	private:
		Swapchain() = default;

		VkDevice       device_{ nullptr };
		VkSwapchainKHR swapchain_{ nullptr };
		Extent2d       extent_{};
		std::vector<VkImageView> image_views_;
	};
}