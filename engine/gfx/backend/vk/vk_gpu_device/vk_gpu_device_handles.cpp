// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_handles.cpp
//
// Purpose:
//   Centralize backend handle allocation for VkGpuDevice.
//
//   Provides simple, monotonic handle generators for RHI-facing resource
//   identifiers (buffers, textures, pipelines, command buffers).
//
//   Handles are lightweight, backend-owned IDs that map to internal registries
//   via `handle.value - 1` indexing. Allocation is intentionally minimal:
//
//     - No reuse or generation counters (v1 simplicity)
//     - No threading guarantees
//     - Lifetime and destruction are managed separately by each subsystem
//
//   This file isolates handle-allocation policy from resource creation and
//   Vulkan object lifetime management, making future changes (reuse, debug
//   labeling, generation IDs) localized and low-risk.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

namespace strata::gfx::vk
{

rhi::BufferHandle VkGpuDevice::allocate_buffer_handle()
{
    return rhi::BufferHandle{next_buffer_++};
}

rhi::TextureHandle VkGpuDevice::allocate_texture_handle()
{
    return rhi::TextureHandle{next_texture_++};
}

rhi::PipelineHandle VkGpuDevice::allocate_pipeline_handle()
{
    return rhi::PipelineHandle{next_pipeline_++};
}

rhi::DescriptorSetLayoutHandle VkGpuDevice::allocate_descriptor_set_layout_handle()
{
    rhi::DescriptorSetLayoutHandle h{next_descriptor_set_layout_++};
    return h;
}

rhi::DescriptorSetHandle VkGpuDevice::allocate_descriptor_set_handle()
{
    rhi::DescriptorSetHandle h{next_descriptor_set_++};
    return h;
}

rhi::CommandBufferHandle VkGpuDevice::encode_cmd_handle(std::uint32_t slot) const noexcept
{
    // Slot is 0..frames_.size()-1. CommandBufferHandle uses 0 as invalid, so store slot+1.
    return rhi::CommandBufferHandle{slot + 1};
}

bool VkGpuDevice::decode_cmd_handle(rhi::CommandBufferHandle cmd,
                                    std::uint32_t&           out_slot) const noexcept
{
    if (!cmd)
        return false;

    std::uint32_t const slot = cmd.value - 1;
    if (slot >= frames_.size())
        return false;

    out_slot = slot;
    return true;
}

} // namespace strata::gfx::vk