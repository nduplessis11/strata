// engine/gfx/src/vulkan/swapchain.cc

#include "strata/gfx/vulkan/swapchain.h"
#include <vulkan/vulkan.h>

namespace strata::gfx {
	Swapchain::Handle::Handle(VkDevice d,
		VkSwapchainKHR s,
		std::vector<VkImageView> vs)
		: device_(d)
		, swapchain_(s)
		, image_views_(std::move(vs)) {
	}

	Swapchain::Handle::~Handle() {
		if (device_ && swapchain_) {
			// Destroy image views first
			for (VkImageView view : image_views_) {
				if (view) {
					vkDestroyImageView(device_, view, nullptr);
				}
			}
			image_views_.clear();

			vkDestroySwapchainKHR(device_, swapchain_, nullptr);
			swapchain_ = nullptr;
			device_ = nullptr;
		}
	}

	Swapchain::Handle::Handle(Handle&& other) noexcept
		: device_(other.device_)
		, swapchain_(other.swapchain_)
		, image_views_(std::move(other.image_views_)) {
		other.device_ = nullptr;
		other.swapchain_ = nullptr;
	}

	Swapchain::Handle& Swapchain::Handle::operator=(Handle&& other) noexcept {
		if (this != &other) {
			// Destroy current resources if any
			if (device_ && swapchain_) {
				for (VkImageView view : image_views_) {
					if (view) {
						vkDestroyImageView(device_, view, nullptr);
					}
				}
				image_views_.clear();
				vkDestroySwapchainKHR(device_, swapchain_, nullptr);
			}

			device_ = other.device_;
			swapchain_ = other.swapchain_;
			image_views_ = std::move(other.image_views_);

			other.device_ = nullptr;
			other.swapchain_ = nullptr;
		}
		return *this;
	}

	Swapchain Swapchain::create(const VulkanContext& ctx, Extent2d window_size) {
		Swapchain sc{};

		// 1) Use ctx.device(), ctx.surface(), ctx.physical_, ctx queue families
		//    to query surface capabilities and pick:
		//      - surface format
		//      - present mode
		//      - extent

		VkDevice device = ctx.device();
		// ... query VkSurfaceCapabilitiesKHR, formats, present modes, etc.

		// 2) Call vkCreateSwapchainKHR(...)
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		// VkSwapchainCreateInfoKHR ci{...};
		// vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);

		// 3) Get swapchain images and create VkImageView for each
		std::vector<VkImageView> views;
		// vkGetSwapchainImagesKHR(...) + loop with vkCreateImageView(...);

		// 4) Fill in the RAII handle + extent
		sc.handle_ = Swapchain::Handle{
			device,
			swapchain,
			std::move(views)
		};
		sc.extent_ = window_size; // or convert from VkExtent2D

		return sc;
	}

}