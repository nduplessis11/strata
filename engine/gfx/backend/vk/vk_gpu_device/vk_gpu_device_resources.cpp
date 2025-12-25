// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_resources.cpp
//
// Purpose:
//   Resource creation/destruction stubs (buffers, textures).
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <cstdint>
#include <cstring> // std::memcpy
#include <optional>
#include <print>

namespace strata::gfx::vk
{

namespace
{

[[nodiscard]] constexpr bool has_flag(rhi::BufferUsage usage, rhi::BufferUsage flag) noexcept
{
    return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(flag)) != 0;
}

[[nodiscard]] VkBufferUsageFlags to_vk_buffer_usage_flags(rhi::BufferUsage usage) noexcept
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

[[nodiscard]] std::optional<std::uint32_t> find_memory_type_index(VkPhysicalDevice      physical,
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
        std::println(stderr, "VkGpuDevice: create_buffer failed (size_bytes == 0)");
        return {};
    }

    // v1: only implement the "host-visible" path (UBO bring-up)
    // Non-host-visible can remain stubbed for now without breaking existing callers.
    if (!desc.host_visible)
    {
        BufferRecord& rec = buffers_[index];
        rec.size_bytes    = desc.size_bytes;
        rec.host_visible  = false;
        // rec.buffer/memory/mapped remain null
        std::println(stderr,
                     "VkGpuDevice: create_buffer({}, {} bytes): non-host-visible buffers not "
                     "implemented yet",
                     handle.value,
                     desc.size_bytes);
        return handle;
    }

    VkDevice const         vk_device   = device_.device();
    VkPhysicalDevice const vk_physical = device_.physical();

    if (vk_device == VK_NULL_HANDLE || vk_physical == VK_NULL_HANDLE)
    {
        std::println(stderr, "VkGpuDevice: create_buffer failed (device/physical is null)");
        buffers_[index] = BufferRecord{};
        return {};
    }

    VkBufferUsageFlags const usage_flags = to_vk_buffer_usage_flags(desc.usage);
    if (usage_flags == 0)
    {
        std::println(stderr, "VkGpuDevice: create_buffer failed (usage flags are NoFlags/unsupported");
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
            std::println(stderr,
                         "VkGpuDevice: create_buffer failed: {} ({})",
                         msg,
                         (std::int32_t)res);
        }
        else
        {
            std::println(stderr, "VkGpuDevice: create_buffer failed: {}", msg);
        }

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
    VkBufferCreateInfo const bci{
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = static_cast<VkDeviceSize>(desc.size_bytes),
        .usage                 = usage_flags,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
    };

    VkResult res = vkCreateBuffer(vk_device, &bci, nullptr, &buffer);
    if (res != VK_SUCCESS)
        return fail("vkCreateBuffer", res);

    // 2) Allocate memory (one allocation per buffer, v1)
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vk_device, buffer, &req);

    VkMemoryPropertyFlags const required_flags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    auto const mem_type_index =
        find_memory_type_index(vk_physical, req.memoryTypeBits, required_flags);
    if (!mem_type_index.has_value())
    {
        return fail(
            "v1: requires HOST_VISIBLE|HOST_COHERENT; add flush path for non-coherent memory");
    }

    VkMemoryAllocateInfo const mai{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = nullptr,
        .allocationSize  = req.size,
        .memoryTypeIndex = mem_type_index.value(),
    };

    res = vkAllocateMemory(vk_device, &mai, nullptr, &memory);
    if (res != VK_SUCCESS)
        return fail("vkAllocateMemory", res);

    res = vkBindBufferMemory(vk_device, buffer, memory, 0);
    if (res != VK_SUCCESS)
        return fail("vkBindBufferMemory", res);

    // 3) Map once and keep mapped (v1 UBO simplicity)
    res = vkMapMemory(vk_device, memory, 0, req.size, 0, &mapped);
    if (res != VK_SUCCESS || mapped == nullptr)
        return fail("vkMapMemory", res);

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

    std::println(stderr,
                 "VkGpuDevice: create_buffer({}, {} bytes) OK (memType={}, usage=0x{:x})",
                 handle.value,
                 desc.size_bytes,
                 mem_type_index.value(),
                 static_cast<std::uint32_t>(usage_flags));

    return handle;
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

rhi::TextureHandle VkGpuDevice::create_texture(rhi::TextureDesc const&)
{
    return allocate_texture_handle();
}

void VkGpuDevice::destroy_texture(rhi::TextureHandle)
{
    // Stub
}

} // namespace strata::gfx::vk
