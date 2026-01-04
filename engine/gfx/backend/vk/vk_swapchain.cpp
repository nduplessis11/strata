// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_swapchain.cpp
//
// Purpose:
//   Implement Vulkan swapchain creation and image view management.
// -----------------------------------------------------------------------------

#include "vk_swapchain.h"

#include "strata/base/diagnostics.h"
#include "vk_check.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace strata::gfx::vk
{

namespace
{

using u32 = std::uint32_t;

void log_err(base::Diagnostics* diag, std::string_view msg)
{
    if (!diag)
        return;
    diag->logger().log(base::LogLevel::Error, "vk.swapchain", msg, std::source_location::current());
    diag->debug_break_on_error();
}

[[nodiscard]]
bool vk_ok(base::Diagnostics* diag, VkResult r, char const* what)
{
    if (r == VK_SUCCESS)
        return true;

    log_err(diag, vk_error_message(what, r));
    return false;
}

[[nodiscard]]
VkFormat to_vk_format(rhi::Format fmt) noexcept
{
    switch (fmt)
    {
    case rhi::Format::B8G8R8A8_UNorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case rhi::Format::R8G8B8A8_UNorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

// Choose a surface format (we prefer SRGB BGRA if available)
[[nodiscard]]
bool choose_surface_format(base::Diagnostics*  diag,
                           VkPhysicalDevice    physical,
                           VkSurfaceKHR        surface,
                           VkFormat            requested_format,
                           VkSurfaceFormatKHR& out)
{
    u32      count = 0;
    VkResult r     = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
    if (!vk_ok(diag, r, "vkGetPhysicalDeviceSurfaceFormatsKHR(count)"))
        return false;

    if (count == 0)
    {
        log_err(diag, "vkGetPhysicalDeviceSurfaceFormatsKHR returned 0 formats");
        return false;
    }

    std::vector<VkSurfaceFormatKHR> formats(count);
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());
    if (!vk_ok(diag, r, "vkGetPhysicalDeviceSurfaceFormatsKHR(list)"))
        return false;

    // Special case: "no preferred format".
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        VkFormat const requested_or_fallback =
            (requested_format != VK_FORMAT_UNDEFINED) ? requested_format : VK_FORMAT_B8G8R8A8_UNORM;

        out = VkSurfaceFormatKHR{requested_or_fallback, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        return true;
    }

    // 1) If caller requested a format, try to honor it.
    if (requested_format != VK_FORMAT_UNDEFINED)
    {
        // Prefer SRGB nonlinear color space (common for SDR).
        for (auto const& f : formats)
        {
            if (f.format == requested_format && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                out = f;
                return true;
            }
        }

        // Otherwise accept any colorspace with the requested format.
        for (auto const& f : formats)
        {
            if (f.format == requested_format)
            {
                out = f;
                return true;
            }
        }
    }

    // 2) Existing preferred default (BGRA UNORM + SRGB nonlinear).
    for (auto const& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            out = f;
            return true;
        }
    }

    // 3) Fallback: first supported.
    out = formats[0];
    return true;
}

// Choose present mode (prefer MAILBOX, else FIFO)
[[nodiscard]] bool choose_present_mode(base::Diagnostics* diag,
                                       VkPhysicalDevice   physical,
                                       VkSurfaceKHR       surface,
                                       bool               vsync,
                                       VkPresentModeKHR&  out)
{
    u32      count = 0;
    VkResult r     = vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
    if (!vk_ok(diag, r, "vkGetPhysicalDeviceSurfacePresentModesKHR(count)"))
        return false;

    if (count == 0)
    {
        // Spec says FIFO is always supported; be robust.
        out = VK_PRESENT_MODE_FIFO_KHR;
        return true;
    }

    std::vector<VkPresentModeKHR> modes(count);
    r = vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());
    if (!vk_ok(diag, r, "vkGetPhysicalDeviceSurfacePresentModesKHR(list)"))
        return false;

    if (!vsync)
    {
        for (auto m : modes)
        {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                out = m;
                return true;
            }
        }
        // Consider IMMEDIATE later; FIFO is fine as fallback.
    }

    out = VK_PRESENT_MODE_FIFO_KHR;
    return true;
}

VkExtent2D choose_extent(VkSurfaceCapabilitiesKHR const& capabilities,
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

VkSwapchainWrapper::VkSwapchainWrapper(VkSwapchainWrapper&& other) noexcept
      : diagnostics_(other.diagnostics_)
      , device_(other.device_)
      , swapchain_(other.swapchain_)
      , image_format_(other.image_format_)
      , extent_(other.extent_)
      , images_(std::move(other.images_))
      , image_views_(std::move(other.image_views_))
{
    other.diagnostics_  = nullptr;
    other.device_       = VK_NULL_HANDLE;
    other.swapchain_    = VK_NULL_HANDLE;
    other.image_format_ = VK_FORMAT_UNDEFINED;
    other.extent_       = {};
}

VkSwapchainWrapper& VkSwapchainWrapper::operator=(VkSwapchainWrapper&& other) noexcept
{
    if (this != &other)
    {
        cleanup();

        diagnostics_  = other.diagnostics_;
        device_       = other.device_;
        swapchain_    = other.swapchain_;
        image_format_ = other.image_format_;
        extent_       = other.extent_;
        images_       = std::move(other.images_);
        image_views_  = std::move(other.image_views_);

        other.diagnostics_  = nullptr;
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

bool VkSwapchainWrapper::init(VkPhysicalDevice          physical,
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
    VkResult const cr = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
    if (!vk_ok(diagnostics_, cr, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
    {
        cleanup();
        return false;
    }

    VkFormat const     requested_format = to_vk_format(desc.format);
    VkSurfaceFormatKHR surface_format{};
    if (!choose_surface_format(diagnostics_, physical, surface, requested_format, surface_format))
    {
        cleanup();
        return false;
    }

    if (diagnostics_ &&
        requested_format != VK_FORMAT_UNDEFINED &&
        surface_format.format != requested_format)
    {
        STRATA_LOG_WARN(diagnostics_->logger(),
                        "vk.swapchain",
                        "Requested swapchain format {} not supported; using {} instead",
                        static_cast<std::int32_t>(requested_format),
                        static_cast<std::int32_t>(surface_format.format));
    }

    VkPresentModeKHR present_mode{};
    if (!choose_present_mode(diagnostics_, physical, surface, desc.vsync, present_mode))
    {
        cleanup();
        return false;
    }

    VkExtent2D const extent = choose_extent(capabilities, desc.size);

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
    VkResult const sr = vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain_);
    if (!vk_ok(diagnostics_, sr, "vkCreateSwapchainKHR"))
    {
        cleanup();
        return false;
    }

    // 5) Get swapchain images
    u32      actual_image_count = 0;
    VkResult ir = vkGetSwapchainImagesKHR(device, swapchain_, &actual_image_count, nullptr);
    if (!vk_ok(diagnostics_, ir, "vkGetSwapchainImagesKHR(count)"))
    {
        cleanup();
        return false;
    }

    images_.resize(actual_image_count);
    ir = vkGetSwapchainImagesKHR(device, swapchain_, &actual_image_count, images_.data());
    if (!vk_ok(diagnostics_, ir, "vkGetSwapchainImagesKHR(list)"))
    {
        cleanup();
        return false;
    }

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

        VkImageView    view = VK_NULL_HANDLE;
        VkResult const vr   = vkCreateImageView(device, &ivci, nullptr, &view);
        if (!vk_ok(diagnostics_, vr, "vkCreateImageView(swapchain)"))
        {
            cleanup(); // safe: destroys views created so far + swapchain
            return false;
        }

        image_views_.push_back(view);
    }

    if (diagnostics_)
    {
        STRATA_LOG_INFO(diagnostics_->logger(),
                        "vk.swapchain",
                        "Swapchain created: {} images, extent {}x{}, vsync {}",
                        static_cast<int>(images_.size()),
                        static_cast<int>(extent_.width),
                        static_cast<int>(extent_.height),
                        desc.vsync ? "on" : "off");
    }
    return true;
}

} // namespace strata::gfx::vk
