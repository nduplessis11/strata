// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_pipeline.cpp
//
// Purpose:
//   Pipeline creation/destruction for the Vulkan backend.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <print>

namespace strata::gfx::vk
{

// --- Pipelines -----------------------------------------------------------

rhi::PipelineHandle VkGpuDevice::create_pipeline(rhi::PipelineDesc const& desc)
{
    if (!swapchain_.valid() || !device_.device())
    {
        return {};
    }

    // Remember the layout handles so cmd_bind_pipeline can rebuild if needed.
    pipeline_set_layout_handles_.assign(desc.set_layouts.begin(), desc.set_layouts.end());

    std::vector<VkDescriptorSetLayout> vk_layouts;
    vk_layouts.reserve(pipeline_set_layout_handles_.size());

    for (auto const h : pipeline_set_layout_handles_)
    {
        VkDescriptorSetLayout const vk_layout = get_vk_descriptor_set_layout(h);
        if (vk_layout == VK_NULL_HANDLE)
        {
            std::println(stderr,
                         "VkGpuDevice: create_pipeline failed (DescriptorSetLayoutHandle invalid)");
            pipeline_set_layout_handles_.clear();
            return {};
        }
        vk_layouts.push_back(vk_layout);
    }

    basic_pipeline_ =
        create_basic_pipeline(device_.device(), swapchain_.image_format(), std::span{vk_layouts});
    if (!basic_pipeline_.valid())
    {
        std::println(stderr, "VkGpuDevice: failed to create basic pipeline");
        pipeline_set_layout_handles_.clear();
        return {};
    }

    return allocate_pipeline_handle();
}

void VkGpuDevice::destroy_pipeline(rhi::PipelineHandle)
{
    // v1: single backend pipeline. Drop the Vulkan objects.
    basic_pipeline_ = BasicPipeline{};

    // IMPORTANT:
    // Do NOT clear pipeline_set_layout_handles_ here.
    // These handles are the "recipe" for rebuilding the pipeline layout after
    // swapchain resize / pipeline invalidation.
}

} // namespace strata::gfx::vk
