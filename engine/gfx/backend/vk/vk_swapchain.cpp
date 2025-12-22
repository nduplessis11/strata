// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_swapchain.cpp
//
// Purpose:
//   Implement Vulkan swapchain creation and image view management.
// -----------------------------------------------------------------------------

#include "vk_swapchain.h"

#include <algorithm>
#include <limits>
#include <print>

namespace strata::gfx::vk
{
namespace
{

using u32 = std::uint32_t;

// Choose a surface format (we prefer SRGB BGRA if available)
VkSurfaceFormatKHR choose_surface_format(
    VkPhysicalDevice physical,
    VkSurfaceKHR     surface)
{
    u32 count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
    if (count == 0)
    {
        // Fallback to something sensible if the driver is weird.
        return VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }

    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());

    // If the surface has no preferred format, just pick one
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        return VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }

    // Prefer SRGB BGRA if available
    for (auto const& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return f;
        }
    }

    // Fallback: first format
    return formats[0];
}

// Choose present mode (prefer MAILBOX, else FIFO)
VkPresentModeKHR choose_present_mode(
    VkPhysicalDevice physical,
    VkSurfaceKHR     surface,
    bool             vsync)
{
    u32 count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
    if (count == 0)
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());

    // If vsync == false we might prefer MAILBOX or IMMEDIATE later.
    if (!vsync)
    {
        for (auto m : modes)
        {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return m;
            }
        }
    }

    // FIFO is always supported.
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(
    VkSurfaceCapabilitiesKHR const& capabilities,
    rhi::Extent2D const&            framebuffer_size)
{
    if (capabilities.currentExtent.width != std::numeric_limits<u32>::max())
    {
        // The surface size is dictated by the window system
        return capabilities.currentExtent;
    }

    VkExtent2D actual{};
    actual.width  = framebuffer_size.width;
    actual.height = framebuffer_size.height;

    actual.width  = std::clamp(actual.width,
                              capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    actual.height = std::clamp(actual.height,
                               capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return actual;
}

} // anonymous namespace

VkSwapchainWrapper::~VkSwapchainWrapper()
{
    cleanup();
}

VkSwapchainWrapper::VkSwapchainWrapper(
    VkSwapchainWrapper&& other) noexcept
    : device_(other.device_),
      swapchain_(other.swapchain_),
      image_format_(other.image_format_),
      extent_(other.extent_),
      images_(std::move(other.images_)),
      image_views_(std::move(other.image_views_))
{
    other.device_       = VK_NULL_HANDLE;
    other.swapchain_    = VK_NULL_HANDLE;
    other.image_format_ = VK_FORMAT_UNDEFINED;
    other.extent_       = {};
}

VkSwapchainWrapper& VkSwapchainWrapper::operator=(
    VkSwapchainWrapper&& other) noexcept
{
    if (this != &other)
    {
        cleanup();

        device_       = other.device_;
        swapchain_    = other.swapchain_;
        image_format_ = other.image_format_;
        extent_       = other.extent_;
        images_       = std::move(other.images_);
        image_views_  = std::move(other.image_views_);

        other.device_       = VK_NULL_HANDLE;
        other.swapchain_    = VK_NULL_HANDLE;
        other.image_format_ = VK_FORMAT_UNDEFINED;
        other.extent_       = {};
    }
    return *this;
}

void VkSwapchainWrapper::cleanup()
{
    if (device_ != VK_NULL_HANDLE && swapchain_ != VK_NULL_HANDLE)
    {
        // Destroy image views first
        for (VkImageView view : image_views_)
        {
            if (view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_, view, nullptr);
            }
        }
        image_views_.clear();
        images_.clear();

        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    device_       = VK_NULL_HANDLE;
    image_format_ = VK_FORMAT_UNDEFINED;
    extent_       = {};
}

bool VkSwapchainWrapper::init(
    VkPhysicalDevice          physical,
    VkDevice                  device,
    VkSurfaceKHR              surface,
    std::uint32_t             graphics_family,
    std::uint32_t             present_family,
    rhi::SwapchainDesc const& desc)
{
    cleanup();

    device_ = device;

    // 1) Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);

    VkSurfaceFormatKHR const surface_format = choose_surface_format(physical, surface);
    VkPresentModeKHR const   present_mode   = choose_present_mode(physical, surface, desc.vsync);
    VkExtent2D const         extent         = choose_extent(capabilities, desc.size);

    image_format_ = surface_format.format;
    extent_       = extent;

    // 2) Decide number of images in the swapchain
    u32 image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
    {
        image_count = capabilities.maxImageCount;
    }

    // 3) Fill swapchain create info
    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface;
    ci.minImageCount    = image_count;
    ci.imageFormat      = surface_format.format;
    ci.imageColorSpace  = surface_format.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    u32 queue_family_indices[2] = {
        graphics_family,
        present_family,
    };

    if (graphics_family != present_family)
    {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = queue_family_indices;
    }
    else
    {
        ci.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        ci.queueFamilyIndexCount = 0;
        ci.pQueueFamilyIndices   = nullptr;
    }

    ci.preTransform   = capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = present_mode;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = VK_NULL_HANDLE; // for simplicity; we recreate from scratch

    // 4) Create the swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkResult       result    = vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);
    if (result != VK_SUCCESS)
    {
        std::println(stderr, "vkCreateSwapchainKHR failed: {}", static_cast<int>(result));
        cleanup();
        return false;
    }

    swapchain_ = swapchain;

    // 5) Get swapchain images
    u32 actual_image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain_, &actual_image_count, nullptr);
    images_.resize(actual_image_count);
    vkGetSwapchainImagesKHR(device, swapchain_, &actual_image_count, images_.data());

    // 6) Create image views
    image_views_.clear();
    image_views_.reserve(images_.size());

    for (VkImage image : images_)
    {
        VkImageViewCreateInfo ivci{};
        ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image                           = image;
        ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format                          = surface_format.format;
        ivci.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel   = 0;
        ivci.subresourceRange.levelCount     = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount     = 1;

        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &ivci, nullptr, &view) != VK_SUCCESS)
        {
            std::println(stderr, "vkCreateImageView failed for swapchain image");
            // Cleanup partially created state
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
            for (VkImageView v : image_views_)
            {
                if (v != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(device_, v, nullptr);
                }
            }
            image_views_.clear();
            images_.clear();
            device_       = VK_NULL_HANDLE;
            image_format_ = VK_FORMAT_UNDEFINED;
            extent_       = {};
            return false;
        }
        image_views_.push_back(view);
    }

    return true;
}

} // namespace strata::gfx::vk
