// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device_pipeline.cpp
//
// Purpose:
//   Pipeline creation/destruction for the Vulkan backend.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <print>

namespace strata::gfx::vk
{

// --- Pipelines -----------------------------------------------------------

rhi::PipelineHandle VkGpuDevice::create_pipeline(rhi::PipelineDesc const&)
{
    if (!swapchain_.valid() || !device_.device())
    {
        return rhi::PipelineHandle{};
    }

    // We ignore PipelineDesc for now and always build the same fullscreen-triangle pipeline.
    basic_pipeline_ = create_basic_pipeline(device_.device(), swapchain_.image_format());
    if (!basic_pipeline_.valid())
    {
        std::println(stderr, "VkGpuDevice: failed to create basic pipeline");
        return rhi::PipelineHandle{};
    }

    return allocate_pipeline_handle();
}

void VkGpuDevice::destroy_pipeline(rhi::PipelineHandle)
{
    // We only keep one backend pipeline; drop it when asked to destroy any handle.
    basic_pipeline_ = BasicPipeline{};
}

} // namespace strata::gfx::vk
