// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_pipeline_basic.cpp
//
// Purpose:
//   Build a simple graphics pipeline for drawing a fullscreen triangle using
//   Vulkan dynamic rendering. This pipeline is used by Renderer2d as its
//   initial "basic" pass.
//
// Design Notes:
//   - This file is internal to the gfx backend and is not included in any
//     public headers. It includes <vulkan/vulkan.h> directly.
//   - BasicPipeline is a small RAII wrapper that owns:
//        - VkPipelineLayout
//        - VkPipeline
//     and holds a non-owning VkDevice pointer used for destruction.
//   - Shader modules are created from SPIR-V on disk and destroyed once the
//     pipeline object has been created.
//   - Viewport/scissor are configured as dynamic state so the pipeline may be
//     reused across resizes by setting them at command-recording time.
// -----------------------------------------------------------------------------

#include "vk_pipeline_basic.h"

#include "strata/base/diagnostics.h"
#include "vk_check.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <fstream>
#include <print>
#include <span>
#include <vector>

namespace strata::gfx::vk
{

namespace
{

void log_vk_error(base::Diagnostics* diag, char const* what, VkResult r) noexcept
{
    if (!diag)
        return;

    diag->logger().log(base::LogLevel::Error,
                       "vk.pipe",
                       vk_error_message(what, r),
                       std::source_location::current());
    diag->debug_break_on_error();
}

char const* non_empty_or_default(char const* path, char const* fallback) noexcept
{
    if (!path || path[0] == '\0')
        return fallback;
    return path;
}

// Load a binary file fully into a buffer.
// The path is interpreted relative to the current working directory
// (usually the executable directory).
std::vector<char> read_binary_file(base::Diagnostics* diag, char const* path)
{
    if (!diag)
        return {};

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        STRATA_LOG_ERROR(diag->logger(),
                         "vk.pipe",
                         "vk_pipeline_basic: failed to open SPIR-V file '{}'",
                         path);
        diag->debug_break_on_error();
        return {};
    }

    std::streamsize const size = file.tellg();
    if (size <= 0)
    {
        STRATA_LOG_ERROR(diag->logger(),
                         "vk.shader",
                         "vk_pipeline_basic: SPIR-V file '{}' is empty",
                         path);
        diag->debug_break_on_error();
        return {};
    }
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<std::size_t>(size));
    if (!file.read(buffer.data(), size))
    {
        STRATA_LOG_ERROR(diag->logger(),
                         "vk.shader",
                         "vk_pipeline_basic: failed to read SPIR-V file '{}'",
                         path);
        diag->debug_break_on_error();
        return {};
    }
    return buffer;
}

// Create a VkShaderModule from SPIR-V bytes.
VkShaderModule create_shader_module(base::Diagnostics*    diag,
                                    VkDevice              device,
                                    std::span<char const> code)
{
    if (code.empty())
    {
        return VK_NULL_HANDLE;
    }

    // SPIR-V must be 4-byte aligned and sized.
    if (code.size() % sizeof(std::uint32_t) != 0)
    {
        STRATA_LOG_ERROR(diag->logger(),
                         "vk.shader",
                         "vk_pipeline_basic: SPIR-V code size is not a multiple of 4");
        diag->debug_break_on_error();
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<std::uint32_t const*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult const r      = vkCreateShaderModule(device, &ci, nullptr, &module);
    if (r != VK_SUCCESS)
    {
        log_vk_error(diag, "vkCreateShaderModule", r);
        return VK_NULL_HANDLE;
    }

    return module;
}

constexpr bool format_has_stencil(VkFormat fmt) noexcept
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

} // namespace

BasicPipeline::~BasicPipeline()
{
    destroy();
}

BasicPipeline::BasicPipeline(BasicPipeline&& other) noexcept
      : device(other.device)
      , layout(other.layout)
      , pipeline(other.pipeline)
{
    other.device   = VK_NULL_HANDLE;
    other.layout   = VK_NULL_HANDLE;
    other.pipeline = VK_NULL_HANDLE;
}

BasicPipeline& BasicPipeline::operator=(BasicPipeline&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    destroy();

    device   = other.device;
    pipeline = other.pipeline;
    layout   = other.layout;

    other.device   = VK_NULL_HANDLE;
    other.pipeline = VK_NULL_HANDLE;
    other.layout   = VK_NULL_HANDLE;

    return *this;
}

BasicPipeline create_basic_pipeline(VkDevice                               device,
                                    VkFormat                               color_format,
                                    base::Diagnostics*                     diag,
                                    std::span<VkDescriptorSetLayout const> set_layouts,
                                    VkFormat                               depth_format,
                                    bool                                   depth_test,
                                    bool                                   depth_write,
                                    char const*                            vertex_shader_path,
                                    char const*                            fragment_shader_path)
{
    if (!diag)
        return {};

    BasicPipeline out{};
    out.device = device;

    if (!device)
    {
        STRATA_LOG_ERROR(diag->logger(), "vk.pipe", "vk_pipeline_basic: device is null");
        diag->debug_break_on_error();
        return out;
    }

    // If paths are null/empty, fall back to historical defaults.
    char const* const vert_path =
        non_empty_or_default(vertex_shader_path, basic_pipeline_default_vertex_shader_path);
    char const* const frag_path =
        non_empty_or_default(fragment_shader_path, basic_pipeline_default_fragment_shader_path);

    // NOTE: These paths assume shaders are copied to <exe>/shaders.
    auto vert_bytes = read_binary_file(diag, vert_path);
    auto frag_bytes = read_binary_file(diag, frag_path);

    if (vert_bytes.empty() || frag_bytes.empty())
    {
        // read_binary_file already logged errors
        return out;
    }

    VkShaderModule vert_module = create_shader_module(diag, device, vert_bytes);
    VkShaderModule frag_module = create_shader_module(diag, device, frag_bytes);
    if (!vert_module || !frag_module)
    {
        if (vert_module)
        {
            vkDestroyShaderModule(device, vert_module, nullptr);
        }
        if (frag_module)
        {
            vkDestroyShaderModule(device, frag_module, nullptr);
        }
        return out;
    }

    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName  = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName  = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

    // No vertex buffers: positions are generated in the vertex shader using gl_VertexIndex.
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_asm{};
    input_asm.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_asm.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_asm.primitiveRestartEnable = VK_FALSE;

    // We will use dynamic viewport/scissor. Pipeline doesn't need static ones.
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = nullptr;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = nullptr;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(std::size(dyn_states));
    dynamic_state.pDynamicStates    = dyn_states;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable        = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode             = VK_POLYGON_MODE_FILL;
    raster.cullMode                = VK_CULL_MODE_BACK_BIT;
    // We flip Y in the projection for Vulkan (positive viewport height).
    // With this convention, our geometry is authored with CCW front faces.
    raster.frontFace       = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;
    raster.lineWidth       = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    msaa.sampleShadingEnable  = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend_attach{};
    blend_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    blend_attach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.logicOpEnable   = VK_FALSE;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_attach;

    VkPipelineLayoutCreateInfo layout_ci{};
    layout_ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.pNext                  = nullptr;
    layout_ci.setLayoutCount         = static_cast<std::uint32_t>(set_layouts.size());
    layout_ci.pSetLayouts            = set_layouts.empty() ? nullptr : set_layouts.data();
    layout_ci.pushConstantRangeCount = 0;
    layout_ci.pPushConstantRanges    = nullptr;

    VkResult r = vkCreatePipelineLayout(device, &layout_ci, nullptr, &out.layout);
    if (r != VK_SUCCESS)
    {
        log_vk_error(diag, "vkCreatePipelineLayout", r);
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        out.destroy();
        return {};
    }

    // Dynamic rendering interface: specify the color attachment format.
    VkPipelineRenderingCreateInfo rendering_ci{};
    rendering_ci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_ci.colorAttachmentCount    = 1;
    rendering_ci.pColorAttachmentFormats = &color_format;

    if (depth_format != VK_FORMAT_UNDEFINED)
    {
        rendering_ci.depthAttachmentFormat = depth_format;
        rendering_ci.stencilAttachmentFormat =
            format_has_stencil(depth_format) ? depth_format : VK_FORMAT_UNDEFINED;
    }
    else
    {
        rendering_ci.depthAttachmentFormat   = VK_FORMAT_UNDEFINED;
        rendering_ci.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    }

    VkPipelineDepthStencilStateCreateInfo  dsci{};
    VkPipelineDepthStencilStateCreateInfo* dsci_ptr = nullptr;

    if (depth_format != VK_FORMAT_UNDEFINED)
    {
        dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        dsci.depthTestEnable  = depth_test ? VK_TRUE : VK_FALSE;
        dsci.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
        dsci.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        dsci.depthBoundsTestEnable = VK_FALSE;
        dsci.stencilTestEnable     = VK_FALSE;
        dsci.minDepthBounds        = 0.0f;
        dsci.maxDepthBounds        = 1.0f;

        dsci_ptr = &dsci;
    }

    VkGraphicsPipelineCreateInfo gp_ci{};
    gp_ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp_ci.pNext               = &rendering_ci;
    gp_ci.stageCount          = static_cast<std::uint32_t>(std::size(stages));
    gp_ci.pStages             = stages;
    gp_ci.pVertexInputState   = &vertex_input;
    gp_ci.pInputAssemblyState = &input_asm;
    gp_ci.pViewportState      = &viewport_state;
    gp_ci.pRasterizationState = &raster;
    gp_ci.pMultisampleState   = &msaa;
    gp_ci.pDepthStencilState  = dsci_ptr;
    gp_ci.pColorBlendState    = &blend;
    gp_ci.pDynamicState       = &dynamic_state;
    gp_ci.layout              = out.layout;
    gp_ci.renderPass          = VK_NULL_HANDLE; // dynamic rendering
    gp_ci.subpass             = 0;
    gp_ci.basePipelineHandle  = VK_NULL_HANDLE;
    gp_ci.basePipelineIndex   = 0;

    r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_ci, nullptr, &out.pipeline);

    // Shader modules can be destroyed immediately after pipeline creation (success or failure).
    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    if (r != VK_SUCCESS)
    {
        log_vk_error(diag, "vkCreateGraphicsPipelines", r);
        out.destroy();
        return {};
    }

    return out;
}

void BasicPipeline::destroy() noexcept
{
    if (pipeline)
    {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (layout)
    {
        vkDestroyPipelineLayout(device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }
    device = VK_NULL_HANDLE;
}

} // namespace strata::gfx::vk
