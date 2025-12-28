// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_pipeline.cpp
//
// Purpose:
//   Pipeline creation/destruction for the Vulkan backend.
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include "strata/base/diagnostics.h"

namespace strata::gfx::vk
{

namespace
{

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

} // namespace

rhi::PipelineHandle VkGpuDevice::create_pipeline(rhi::PipelineDesc const& desc)
{
    using namespace strata::base;

    if (!diagnostics_)
        return {};

    auto& diag = *diagnostics_;

    if (!swapchain_.valid() || !device_.device())
        return {};

    // Remember the layout handles so cmd_bind_pipeline can rebuild if needed.
    pipeline_set_layout_handles_.assign(desc.set_layouts.begin(), desc.set_layouts.end());

    std::vector<VkDescriptorSetLayout> vk_layouts;
    vk_layouts.reserve(pipeline_set_layout_handles_.size());

    for (auto const h : pipeline_set_layout_handles_)
    {
        VkDescriptorSetLayout const vk_layout = get_vk_descriptor_set_layout(h);
        if (vk_layout == VK_NULL_HANDLE)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.pipeline",
                             "create_pipeline: invalid DescriptorSetLayoutHandle in recipe");
            diag.debug_break_on_error();
            pipeline_set_layout_handles_.clear();
            return {};
        }
        vk_layouts.push_back(vk_layout);
    }

    // Store the pipeline recipe bits needed for swapchain-resize rebuild.
    if (desc.depth_format == rhi::Format::Unknown)
    {
        if (desc.depth_test || desc.depth_write)
        {
            STRATA_LOG_WARN(diag.logger(),
                            "vk.pipeline",
                            "create_pipeline: depth_test/depth_write set but depth_format is "
                            "Unknown; ignoring depth state");
        }
        basic_pipeline_depth_format_ = VK_FORMAT_UNDEFINED;
        basic_pipeline_depth_test_   = false;
        basic_pipeline_depth_write_  = false;
    }
    else
    {
        VkFormat const vk_depth = to_vk_format(desc.depth_format);
        if (vk_depth == VK_FORMAT_UNDEFINED)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.pipeline",
                             "create_pipeline: unsupported depth format");
            diag.debug_break_on_error();
            pipeline_set_layout_handles_.clear();
            return {};
        }

        basic_pipeline_depth_format_ = vk_depth;
        basic_pipeline_depth_test_   = desc.depth_test;
        basic_pipeline_depth_write_  = desc.depth_write;
    }

    basic_pipeline_ = create_basic_pipeline(device_.device(),
                                            swapchain_.image_format(),
                                            &diag,
                                            std::span{vk_layouts},
                                            basic_pipeline_depth_format_,
                                            basic_pipeline_depth_test_,
                                            basic_pipeline_depth_write_);

    if (!basic_pipeline_.valid())
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.pipeline",
                         "create_pipeline: create_basic_pipeline failed");
        diag.debug_break_on_error();
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
    // Do NOT clear pipeline_set_layout_handles_ (it is the rebuild recipe).
    // Do NOT clear basic_pipeline_depth_* (it is also part of the rebuild recipe).
}

} // namespace strata::gfx::vk
