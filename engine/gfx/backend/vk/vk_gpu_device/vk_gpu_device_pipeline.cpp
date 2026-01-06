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

VkVertexInputRate to_vk_input_rate(rhi::VertexInputRate r) noexcept
{
    switch (r)
    {
    case rhi::VertexInputRate::Vertex:
        return VK_VERTEX_INPUT_RATE_VERTEX;
    case rhi::VertexInputRate::Instance:
        return VK_VERTEX_INPUT_RATE_INSTANCE;
    }
    return VK_VERTEX_INPUT_RATE_VERTEX;
}

VkFormat to_vk_vertex_format(rhi::VertexFormat f) noexcept
{
    switch (f)
    {
    case rhi::VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case rhi::VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

bool is_non_empty(char const* s) noexcept
{
    return (s != nullptr) && (s[0] != '\0');
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

    // Store vertex input recipe for rebuild.
    pipeline_vertex_bindings_.assign(desc.vertex_bindings.begin(), desc.vertex_bindings.end());
    pipeline_vertex_attributes_.assign(desc.vertex_attributes.begin(),
                                       desc.vertex_attributes.end());

    // Convert vertex input recipe to Vk arrays for pipeline creation.
    std::vector<VkVertexInputBindingDescription> vk_bindings;
    vk_bindings.reserve(pipeline_vertex_bindings_.size());
    for (auto const& b : pipeline_vertex_bindings_)
    {
        VkVertexInputBindingDescription vb{};
        vb.binding   = b.binding;
        vb.stride    = b.stride;
        vb.inputRate = to_vk_input_rate(b.rate);
        vk_bindings.push_back(vb);
    }

    std::vector<VkVertexInputAttributeDescription> vk_attrs;
    vk_attrs.reserve(pipeline_vertex_attributes_.size());
    for (auto const& a : pipeline_vertex_attributes_)
    {
        VkFormat const vkf = to_vk_vertex_format(a.format);
        if (vkf == VK_FORMAT_UNDEFINED)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.pipeline",
                             "create_pipeline: unsupported vertex attribute format");
            diag.debug_break_on_error();
            pipeline_set_layout_handles_.clear();
            pipeline_vertex_bindings_.clear();
            pipeline_vertex_attributes_.clear();
            return {};
        }

        VkVertexInputAttributeDescription va{};
        va.location = a.location;
        va.binding  = a.binding;
        va.format   = vkf;
        va.offset   = a.offset;
        vk_attrs.push_back(va);
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

    // Shader paths are also part of the rebuild recipe.
    // If the caller doesn't provide them, we fall back to the historical defaults
    // used by vk_pipeline_basic
    basic_pipeline_vertex_shader_path_ = is_non_empty(desc.vertex_shader_path)
        ? desc.vertex_shader_path
        : basic_pipeline_default_vertex_shader_path;

    basic_pipeline_fragment_shader_path_ = is_non_empty(desc.fragment_shader_path)
        ? desc.fragment_shader_path
        : basic_pipeline_default_fragment_shader_path;

    basic_pipeline_ = create_basic_pipeline(device_.device(),
                                            swapchain_.image_format(),
                                            &diag,
                                            std::span{vk_layouts},
                                            basic_pipeline_depth_format_,
                                            basic_pipeline_depth_test_,
                                            basic_pipeline_depth_write_,
                                            basic_pipeline_vertex_shader_path_.c_str(),
                                            basic_pipeline_fragment_shader_path_.c_str(),
                                            std::span{vk_bindings},
                                            std::span{vk_attrs});

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
    // Do NOT clear basic_pipeline_*_shader_path (it is also part of the rebuild recipe).
    // Do NOT clear vertex input recipe (it is also part of the rebuild recipe).
}

} // namespace strata::gfx::vk
