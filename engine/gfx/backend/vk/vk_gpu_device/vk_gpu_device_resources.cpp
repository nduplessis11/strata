// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_resources.cpp
//
// Purpose:
//   Resource creation/destruction (buffers, textures).
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include "../vk_check.h"
#include "strata/base/diagnostics.h"

#include <cstdint>
#include <cstring> // std::memcpy
#include <optional>

namespace strata::gfx::vk
{

namespace
{

[[nodiscard]]
constexpr bool has_flag(rhi::BufferUsage usage, rhi::BufferUsage flag) noexcept
{
    return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(flag)) != 0;
}

[[nodiscard]]
constexpr bool has_flag(rhi::TextureUsage usage, rhi::TextureUsage flag) noexcept
{
    return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(flag)) != 0;
}

[[nodiscard]]
VkBufferUsageFlags to_vk_buffer_usage_flags(rhi::BufferUsage usage) noexcept
{
    VkBufferUsageFlags out = 0;

    if (has_flag(usage, rhi::BufferUsage::Vertex))
        out |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    if (has_flag(usage, rhi::BufferUsage::Index))
        out |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    if (has_flag(usage, rhi::BufferUsage::Uniform))
        out |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    // Treat "Upload" as a hint this buffer may be used as a transfer source.
    if (has_flag(usage, rhi::BufferUsage::Upload))
        out |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    return out;
}

[[nodiscard]]
VkImageUsageFlags to_vk_image_usage_flags(rhi::TextureUsage usage) noexcept
{
    VkImageUsageFlags out = 0;

    if (has_flag(usage, rhi::TextureUsage::Sampled))
        out |= VK_IMAGE_USAGE_SAMPLED_BIT;

    if (has_flag(usage, rhi::TextureUsage::ColorAttachment))
        out |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (has_flag(usage, rhi::TextureUsage::DepthStencil))
        out |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    return out;
}

[[nodiscard]]
VkFormat to_vk_format(rhi::Format fmt) noexcept
{
    switch (fmt)
    {
    case rhi::Format::R8G8B8A8_UNorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case rhi::Format::B8G8R8A8_UNorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case rhi::Format::D24_UNorm_S8_UInt:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case rhi::Format::D32_SFloat:
        return VK_FORMAT_D32_SFLOAT;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

[[nodiscard]]
constexpr bool is_depth_format(VkFormat fmt) noexcept
{
    switch (fmt)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

[[nodiscard]]
constexpr bool is_stencil_format(VkFormat fmt) noexcept
{
    switch (fmt)
    {
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

[[nodiscard]]
VkImageAspectFlags infer_aspect_mask(VkFormat fmt, rhi::TextureUsage usage) noexcept
{
    // If caller explicitly wants a depth/stencil attachment, prefer depth/stencil aspects.
    if (has_flag(usage, rhi::TextureUsage::DepthStencil) || is_depth_format(fmt))
    {
        VkImageAspectFlags aspect = 0;
        aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (is_stencil_format(fmt))
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        return aspect;
    }

    // Otherwise assume color.
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]]
std::optional<std::uint32_t> find_memory_type_index(VkPhysicalDevice      physical,
                                                    std::uint32_t         type_bits,
                                                    VkMemoryPropertyFlags required)
{
    if (physical == VK_NULL_HANDLE)
        return std::nullopt;

    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);

    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
    {
        if ((type_bits & 1u << i) == 0)
            continue;

        VkMemoryPropertyFlags const flags = mem_props.memoryTypes[i].propertyFlags;
        if ((flags & required) == required)
            return i;
    }

    return std::nullopt;
}

} // namespace

// --- Buffers -------------------------------------------------------------

rhi::BufferHandle VkGpuDevice::create_buffer(rhi::BufferDesc const&     desc,
                                             std::span<std::byte const> initial_data)
{
    using namespace strata::base;

    if (!diagnostics_)
        return {};

    auto& diag = *diagnostics_;

    // Keep handle allocation + registry slot creation consistent
    rhi::BufferHandle const handle = allocate_buffer_handle();
    std::size_t const       index  = static_cast<std::size_t>(handle.value - 1);

    if (index >= buffers_.size())
        buffers_.resize(index + 1);

    // Alwats start from a clean slot.
    buffers_[index] = BufferRecord{};

    // Basic validation
    if (desc.size_bytes == 0)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.buf", "create_buffer failed: size_bytes == 0");
        return {};
    }

    // v1: Non-host-visible can remain stubbed for now without breaking existing callers.
    if (!desc.host_visible)
    {
        STRATA_LOG_WARN(diag.logger(),
                        "vk.buf",
                        "create_buffer({}, {} bytes): non-host-visible buffers not implemented yet",
                        handle.value,
                        desc.size_bytes);

        BufferRecord& rec = buffers_[index];
        rec.size_bytes    = desc.size_bytes;
        rec.host_visible  = false;
        // rec.buffer/memory/mapped remain null

        return handle;
    }

    VkDevice const         vk_device   = device_.device();
    VkPhysicalDevice const vk_physical = device_.physical();

    if (vk_device == VK_NULL_HANDLE || vk_physical == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.buf", "create_buffer failed: device/physical is null");
        buffers_[index] = BufferRecord{};
        return {};
    }

    VkBufferUsageFlags const usage_flags = to_vk_buffer_usage_flags(desc.usage);
    if (usage_flags == 0)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.buf", "create_buffer failed: unsupported usage flags");
        buffers_[index] = BufferRecord{};
        return {};
    }

    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void*          mapped = nullptr;

    auto fail = [&](char const* msg, VkResult res = VK_SUCCESS) -> rhi::BufferHandle
    {
        if (res != VK_SUCCESS)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.buf",
                             "{} ({})",
                             msg,
                             ::strata::gfx::vk::to_string(res));
        }
        else
        {
            STRATA_LOG_ERROR(diag.logger(), "vk.buf", "{}", msg);
        }
        diag.debug_break_on_error();

        if (mapped != nullptr && memory != VK_NULL_HANDLE)
        {
            vkUnmapMemory(vk_device, memory);
            mapped = nullptr;
        }
        if (buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(vk_device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(vk_device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }

        buffers_[index] = BufferRecord{};
        return {};
    };

    // 1) Create buffer
    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = static_cast<VkDeviceSize>(desc.size_bytes);
    bci.usage       = usage_flags;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = vkCreateBuffer(vk_device, &bci, nullptr, &buffer);
    if (res != VK_SUCCESS)
        return fail("vkCreateBuffer failed", res);

    // 2) Allocate memory (one allocation per buffer, v1)
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vk_device, buffer, &req);

    VkMemoryPropertyFlags const required_flags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    auto const mem_type_index =
        find_memory_type_index(vk_physical, req.memoryTypeBits, required_flags);
    if (!mem_type_index.has_value())
    {
        return fail("No HOST_VISIBLE|HOST_COHERENT memory type found (v1 requires coherent)");
    }

    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = mem_type_index.value();

    res = vkAllocateMemory(vk_device, &mai, nullptr, &memory);
    if (res != VK_SUCCESS)
        return fail("vkAllocateMemory failed", res);

    res = vkBindBufferMemory(vk_device, buffer, memory, 0);
    if (res != VK_SUCCESS)
        return fail("vkBindBufferMemory failed", res);

    // 3) Map once and keep mapped (v1 UBO simplicity)
    res = vkMapMemory(vk_device, memory, 0, req.size, 0, &mapped);
    if (res != VK_SUCCESS || mapped == nullptr)
        return fail("vkMapMemory failed", res);

    // 4) Initial data upload (host coherent so no flush needed)
    if (!initial_data.empty())
    {
        if (static_cast<std::uint64_t>(initial_data.size_bytes()) > desc.size_bytes)
            return fail("initial_data larger than buffer size");

        std::memcpy(mapped, initial_data.data(), initial_data.size_bytes());
    }

    // 5) Commit into registry
    BufferRecord rec{};
    rec.buffer       = buffer;
    rec.memory       = memory;
    rec.size_bytes   = desc.size_bytes;
    rec.mapped       = mapped;
    rec.host_visible = true;

    buffers_[index] = rec;

    STRATA_LOG_DEBUG(diag.logger(),
                     "vk.buf",
                     "create_buffer({}, {} bytes) OK (memType={}, usage=0x{:x})",
                     handle.value,
                     desc.size_bytes,
                     mem_type_index.value(),
                     static_cast<std::uint32_t>(usage_flags));

    return handle;
}

rhi::FrameResult VkGpuDevice::write_buffer(rhi::BufferHandle          dst,
                                           std::span<std::byte const> data,
                                           std::uint64_t              offset_bytes)
{
    using namespace strata::base;
    using rhi::FrameResult;

    if (!diagnostics_)
        return FrameResult::Error;

    auto& diag = *diagnostics_;

    if (!dst)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.buf", "write_buffer failed: dst handle is invalid");
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    // Writing zero bytes is a no-op (useful for callers with conditional updates)
    if (data.empty())
        return FrameResult::Ok;

    std::size_t const index = static_cast<std::size_t>(dst.value - 1);
    if (index >= buffers_.size())
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.buf",
                         "write_bufffer failed: handle {} out of range (buffers={})",
                         dst.value,
                         buffers_.size());
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    BufferRecord& rec = buffers_[index];

    if (!rec.host_visible || rec.mapped == nullptr)
    {
        STRATA_LOG_ERROR(
            diag.logger(),
            "vk.buf",
            "write_buffer failed: buffer {} is not host-visible/mapped (v1 requires host_visible)",
            dst.value);
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    std::uint64_t const size = static_cast<std::uint64_t>(data.size_bytes());

    if (offset_bytes > rec.size_bytes)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.buf",
                         "write_buffer failed: offset {} out of bounds (size={}) for buffer {}",
                         offset_bytes,
                         rec.size_bytes,
                         dst.value);
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    if (size > (rec.size_bytes - offset_bytes))
    {
        STRATA_LOG_ERROR(
            diag.logger(),
            "vk.buf",
            "write_buffer failed: write {} bytes at offset {} exceeds buffer {} size {}",
            size,
            offset_bytes,
            dst.value,
            rec.size_bytes);
        diag.debug_break_on_error();
        return FrameResult::Error;
    }

    auto* dst_bytes = static_cast<std::byte*>(rec.mapped) + offset_bytes;
    std::memcpy(dst_bytes, data.data(), data.size_bytes());

    // v1 guarantee: host-visible buffers are allocated with HOST_COHERENT in create_buffer(),
    // so no vkFlushMappedMemoryRanges is required here.
    return FrameResult::Ok;
}

void VkGpuDevice::destroy_buffer(rhi::BufferHandle handle)
{
    if (!handle)
        return;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= buffers_.size())
        return;

    BufferRecord& rec = buffers_[index];

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
    {
        // Can't call Vulkan, but we MUST invalidate our registry entry.
        rec = BufferRecord{};
        return;
    }

    // If PR B is implemented, these will be real.
    if (rec.mapped != nullptr && rec.memory != VK_NULL_HANDLE)
    {
        vkUnmapMemory(vk_device, rec.memory);
    }

    if (rec.buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(vk_device, rec.buffer, nullptr);
    }

    if (rec.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(vk_device, rec.memory, nullptr);
    }

    // Always clear the slot to avoid stale handles / double-destroys.
    rec = BufferRecord{};
}

void VkGpuDevice::cleanup_buffers()
{
    VkDevice vk_device = device_.device();

    if (vk_device != VK_NULL_HANDLE)
    {
        for (auto& rec : buffers_)
        {
            if (rec.mapped != nullptr)
            {
                vkUnmapMemory(vk_device, rec.memory);
                rec.mapped = nullptr;
            }
            if (rec.buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(vk_device, rec.buffer, nullptr);
                rec.buffer = VK_NULL_HANDLE;
            }
            if (rec.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(vk_device, rec.memory, nullptr);
                rec.memory = VK_NULL_HANDLE;
            }
        }
    }

    buffers_.clear();
}

VkBuffer VkGpuDevice::get_vk_buffer(rhi::BufferHandle handle) const noexcept
{
    if (!handle)
        return VK_NULL_HANDLE;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= buffers_.size())
        return VK_NULL_HANDLE;

    return buffers_[index].buffer;
}

// --- Textures ------------------------------------------------------------

rhi::TextureHandle VkGpuDevice::create_texture(rhi::TextureDesc const& desc)
{
    using namespace strata::base;

    if (!diagnostics_)
        return {};

    auto& diag = *diagnostics_;

    if (desc.size.width == 0 || desc.size.height == 0)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.tex", "create_texture failed: size is 0");
        diag.debug_break_on_error();
        return {};
    }

    if (desc.mip_levels == 0)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.tex", "create_texture failed: mip_levels == 0");
        diag.debug_break_on_error();
        return {};
    }

    VkDevice const         vk_device   = device_.device();
    VkPhysicalDevice const vk_physical = device_.physical();

    if (vk_device == VK_NULL_HANDLE || vk_physical == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.tex", "create_texture failed: device/physical is null");
        diag.debug_break_on_error();
        return {};
    }

    VkFormat const vk_format = to_vk_format(desc.format);
    if (vk_format == VK_FORMAT_UNDEFINED)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.tex", "create_texture failed: unsupported format");
        diag.debug_break_on_error();
        return {};
    }

    VkImageUsageFlags const usage_flags = to_vk_image_usage_flags(desc.usage);
    if (usage_flags == 0)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.tex", "create_texture failed: usage flags == 0");
        diag.debug_break_on_error();
        return {};
    }

    rhi::TextureHandle const handle = allocate_texture_handle();
    std::size_t const        index  = static_cast<std::size_t>(handle.value - 1);

    if (index >= textures_.size())
        textures_.resize(index + 1);

    textures_[index] = TextureRecord{};

    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;

    auto fail = [&](char const* msg, VkResult res = VK_SUCCESS) -> rhi::TextureHandle
    {
        if (res != VK_SUCCESS)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.tex",
                             "{} ({})",
                             msg,
                             ::strata::gfx::vk::to_string(res));
        }
        else
        {
            STRATA_LOG_ERROR(diag.logger(), "vk.tex", "{}", msg);
        }
        diag.debug_break_on_error();

        if (view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(vk_device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (image != VK_NULL_HANDLE)
        {
            vkDestroyImage(vk_device, image, nullptr);
            image = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(vk_device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }

        textures_[index] = TextureRecord{};
        return {};
    };

    // 1) Create image (optimal tiling, device local)
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = vk_format;
    ici.extent.width  = desc.size.width;
    ici.extent.height = desc.size.height;
    ici.extent.depth  = 1;
    ici.mipLevels     = desc.mip_levels;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = usage_flags;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = vkCreateImage(vk_device, &ici, nullptr, &image);
    if (res != VK_SUCCESS)
        return fail("vkCreateImage failed", res);

    // 2) Allocate memory
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(vk_device, image, &req);

    VkMemoryPropertyFlags const required_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto const mem_type_index =
        find_memory_type_index(vk_physical, req.memoryTypeBits, required_flags);
    if (!mem_type_index.has_value())
        return fail("No DEVICE_LOCAL memory type found for image");

    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = mem_type_index.value();

    res = vkAllocateMemory(vk_device, &mai, nullptr, &memory);
    if (res != VK_SUCCESS)
        return fail("vkAllocateMemory failed", res);

    res = vkBindImageMemory(vk_device, image, memory, 0);
    if (res != VK_SUCCESS)
        return fail("vkBindImageMemory failed", res);

    // 3) Create view
    VkImageAspectFlags const aspect = infer_aspect_mask(vk_format, desc.usage);

    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = vk_format;
    vci.subresourceRange.aspectMask     = aspect;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = desc.mip_levels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;

    res = vkCreateImageView(vk_device, &vci, nullptr, &view);
    if (res != VK_SUCCESS)
        return fail("vkCreateImageView failed", res);

    // 4) Commit record
    TextureRecord rec{};
    rec.image       = image;
    rec.memory      = memory;
    rec.view        = view;
    rec.extent      = {desc.size.width, desc.size.height};
    rec.format      = vk_format;
    rec.aspect_mask = aspect;
    rec.layout      = VK_IMAGE_LAYOUT_UNDEFINED;
    rec.usage       = desc.usage;
    rec.mip_levels  = desc.mip_levels;

    textures_[index] = rec;

    STRATA_LOG_DEBUG(diag.logger(),
                     "vk.tex",
                     "create_texture({}, {}x{}, fmt={}, usage=0x{:x}, mip={}) OK",
                     handle.value,
                     desc.size.width,
                     desc.size.height,
                     static_cast<std::int32_t>(vk_format),
                     static_cast<std::uint32_t>(usage_flags),
                     desc.mip_levels);

    return handle;
}

void VkGpuDevice::destroy_texture(rhi::TextureHandle handle)
{
    if (!handle)
        return;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= textures_.size())
        return;

    TextureRecord& rec = textures_[index];

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
    {
        // Can't call Vulkan, but we MUST invalidate our registry entry.
        rec = TextureRecord{};
        return;
    }

    if (rec.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(vk_device, rec.view, nullptr);
        rec.view = VK_NULL_HANDLE;
    }

    if (rec.image != VK_NULL_HANDLE)
    {
        vkDestroyImage(vk_device, rec.image, nullptr);
        rec.image = VK_NULL_HANDLE;
    }

    if (rec.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(vk_device, rec.memory, nullptr);
        rec.memory = VK_NULL_HANDLE;
    }

    rec = TextureRecord{};
}

void VkGpuDevice::cleanup_textures()
{
    VkDevice vk_device = device_.device();
    if (vk_device != VK_NULL_HANDLE)
    {
        for (auto& rec : textures_)
        {
            if (rec.view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(vk_device, rec.view, nullptr);
                rec.view = VK_NULL_HANDLE;
            }
            if (rec.image != VK_NULL_HANDLE)
            {
                vkDestroyImage(vk_device, rec.image, nullptr);
                rec.image = VK_NULL_HANDLE;
            }
            if (rec.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(vk_device, rec.memory, nullptr);
                rec.memory = VK_NULL_HANDLE;
            }
            rec = TextureRecord{};
        }
    }
    textures_.clear();
}

VkImage VkGpuDevice::get_vk_image(rhi::TextureHandle handle) const noexcept
{
    if (!handle)
        return VK_NULL_HANDLE;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= textures_.size())
        return VK_NULL_HANDLE;

    return textures_[index].image;
}

VkImageView VkGpuDevice::get_vk_image_view(rhi::TextureHandle handle) const noexcept
{
    if (!handle)
        return VK_NULL_HANDLE;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= textures_.size())
        return VK_NULL_HANDLE;

    return textures_[index].view;
}

VkImageLayout VkGpuDevice::get_vk_image_layout(rhi::TextureHandle handle) const noexcept
{
    if (!handle)
        return VK_IMAGE_LAYOUT_UNDEFINED;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= textures_.size())
        return VK_IMAGE_LAYOUT_UNDEFINED;

    return textures_[index].layout;
}

void VkGpuDevice::set_vk_image_layout(rhi::TextureHandle handle, VkImageLayout layout) noexcept
{
    if (!handle)
        return;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= textures_.size())
        return;

    textures_[index].layout = layout;
}

} // namespace strata::gfx::vk
