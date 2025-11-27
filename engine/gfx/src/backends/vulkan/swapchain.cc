// engine/gfx/src/backends/vulkan/swapchain.cc
// 
// Swapchain images vs. image views:
//
// vkGetSwapchainImagesKHR gives us VkImage handles representing the raw GPU
// pixel storage owned by the swapchain. These images are *not* directly usable
// as framebuffer attachments or shader resources.
//
// To use a VkImage in a render pass, framebuffer, or descriptor set, we must
// create a VkImageView that describes how we will access the image (2D view,
// color aspect, mip levels, array layers, etc.).
//
// We do *not* destroy swapchain VkImages-those are owned and freed by the
// swapchain itself. We *do* destroy the VkImageViews we create for each image,
// and we must destroy them *before* destroying the swapchain.

#include "strata/gfx/vulkan/swapchain.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <print>

namespace strata::gfx {
    namespace {
        using u32 = std::uint32_t;

        // Choose a surface format (we prefer SRGB or BGRA if available)
        VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice physical, VkSurfaceKHR surface) {
            u32 count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());

            // If the surface has no preferred format, just pick one
            if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
                return VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
            }

            // Prefer SRGB BGRA if available
            for (const auto& f : formats) {
                if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    return f;
                }
            }

            // Fallback: first format
            return formats[0];
        }

        // Choose present mode (for starters, use FIFO which is always supported)
        VkPresentModeKHR choose_present_mode(VkPhysicalDevice physical, VkSurfaceKHR surface) {
            u32 count = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
            std::vector<VkPresentModeKHR> modes(count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());

            // We could prefer MAILBOX or IMMEDIATE, but FIFO is always available and vsynced.
            for (auto m : modes) {
                if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
                    return m; // nice low-latency mode if present
                }
            }
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities, Extent2d window_size) {
            if (capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
                // The surface size is dictated by the window system
                return capabilities.currentExtent;
            }

            VkExtent2D actual{};
            actual.width = static_cast<u32>(window_size.width);
            actual.height = static_cast<u32>(window_size.height);

            actual.width = std::clamp(actual.width,
                capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width);
            actual.height = std::clamp(actual.height,
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height);
            return actual;
        }
    } // anonymous namespace 

    Swapchain::Handle::Handle(VkDevice d, VkSwapchainKHR s, std::vector<VkImage> imgs, std::vector<VkImageView> vs)
        : device_(d)
        , swapchain_(s)
        , images_(std::move(imgs))
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
        , images_(std::move(other.images_))
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
            images_ = std::move(other.images_);
            image_views_ = std::move(other.image_views_);

            other.device_ = nullptr;
            other.swapchain_ = nullptr;
        }
        return *this;
    }

    Swapchain Swapchain::create(const VulkanContext& ctx, Extent2d window_size, VkSwapchainKHR old_swapchain) {
        Swapchain sc{};

        VkDevice         device = ctx.device();
        VkSurfaceKHR     surface = ctx.surface();
        VkPhysicalDevice physical = ctx.physical_device();

        // 1) Query surface capabilites
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);

        VkSurfaceFormatKHR surface_format = choose_surface_format(physical, surface);
        VkPresentModeKHR   present_mode = choose_present_mode(physical, surface);
        VkExtent2D         extent = choose_extent(capabilities, window_size);

        sc.color_format_bits_ = static_cast<std::uint32_t>(surface_format.format);

        // 2) Decide how many images in the swapchain
        u32 image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
            image_count = capabilities.maxImageCount;
        }

        // 3) Fill swapchain create info
        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface;
        ci.minImageCount = image_count;
        ci.imageFormat = surface_format.format;
        ci.imageColorSpace = surface_format.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // How we share images between queues (graphics/present)
        u32 queueFamilyIndices[2] = {
            ctx.graphics_family_index(),
            ctx.present_family_index()
        };

        if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ci.queueFamilyIndexCount = 0;
            ci.pQueueFamilyIndices = nullptr;
        }

        ci.preTransform = capabilities.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = present_mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = old_swapchain;

        // 4) Create the swapchain
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkResult result = vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);
        if (result != VK_SUCCESS) {
            // creation failed; return an "empty" Swapchain (handle_.valid() == false)
            std::println(stderr, "vkCreateSwapchainKHR failed: {}", static_cast<int>(result));
            return sc;
        }

        // 5) Get swapchain images
        u32 actual_image_count = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &actual_image_count, nullptr);
        std::vector<VkImage> images(actual_image_count);
        vkGetSwapchainImagesKHR(device, swapchain, &actual_image_count, images.data());

        // 6) Create image views
        std::vector<VkImageView> views;
        views.reserve(images.size());

        for (VkImage image : images) {
            VkImageViewCreateInfo ivci{};
            ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ivci.image = image;
            ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivci.format = surface_format.format;

            ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ivci.subresourceRange.baseMipLevel = 0;
            ivci.subresourceRange.levelCount = 1;
            ivci.subresourceRange.baseArrayLayer = 0;
            ivci.subresourceRange.layerCount = 1;

            VkImageView view = VK_NULL_HANDLE;
            if (vkCreateImageView(device, &ivci, nullptr, &view) != VK_SUCCESS) {
                // if any view creation fails, we bail; Handle's dtor will clean up
                // the ones that succeeded as long as we only assign handle_ after
                // everything is ready.
                // For simplicity here, we'll early-return empty sc.
                return sc;
            }
            views.push_back(view);
        }

        // 7) Fill in the RAII handle + extent
        sc.handle_ = Swapchain::Handle{
            device,
            swapchain,
            std::move(images),
            std::move(views)
        };
        sc.extent_ = Extent2d{
            static_cast<int>(extent.width),
            static_cast<int>(extent.height)
        };
        return sc;
    }
}
