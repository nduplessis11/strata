// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device_resources.cpp
//
// Purpose:
//   Resource creation/destruction stubs (buffers, textures).
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

namespace strata::gfx::vk
{

// --- Buffers -------------------------------------------------------------

rhi::BufferHandle VkGpuDevice::create_buffer(rhi::BufferDesc const&, std::span<std::byte const>)
{
    // Not used by Render2D yet; just hand out a unique handle for now.
    return allocate_buffer_handle();
}

void VkGpuDevice::destroy_buffer(rhi::BufferHandle)
{
    // Stub for now
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
