// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_resources.cpp
//
// Purpose:
//   Resource creation/destruction stubs (buffers, textures).
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

namespace strata::gfx::vk
{

// --- Buffers -------------------------------------------------------------

rhi::BufferHandle VkGpuDevice::create_buffer(rhi::BufferDesc const&     desc,
                                             std::span<std::byte const> initial_data)
{
    (void)initial_data;

    rhi::BufferHandle const handle = allocate_buffer_handle();
    std::size_t const       index  = static_cast<std::size_t>(handle.value - 1);

    if (index >= buffers_.size())
        buffers_.resize(index + 1);

    BufferRecord& rec = buffers_[index];
    rec               = BufferRecord{}; // reset slot
    rec.size_bytes    = desc.size_bytes;
    rec.host_visible  = desc.host_visible;

    // rec.buffer/memory/mapped stay null until PR B.

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
