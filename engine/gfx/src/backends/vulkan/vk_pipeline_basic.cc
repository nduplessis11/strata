// -----------------------------------------------------------------------------
// engine/gfx/src/backends/vulkan/vk_pipeline_basic.cc
//
// Purpose:
//   Build a simple graphics pipeline for drawing a fullscreen triangle using
//   Vulkan dynamic rendering. This pipeline is used by Renderer2d as its
//   initial "basic" pass.
//
// Design Notes:
//   • This file is internal to the gfx backend and is not included in any
//     public headers. It includes <vulkan/vulkan.h> directly.
//   • BasicPipeline is a small RAII wrapper that owns:
//        - VkPipelineLayout
//        - VkPipeline
//     and holds a non-owning VkDevice pointer used for destruction.
//   • Shader modules are created from SPIR-V on disk and destroyed once the
//     pipeline object has been created.
//   • Viewport/scissor are configured as dynamic state so the pipeline may be
//     reused across resizes by setting them at command-recording time.
// -----------------------------------------------------------------------------

#include "backends/vulkan/vk_pipeline_basic.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <fstream>
#include <print>
#include <span>
#include <vector>

namespace {

    // Load a binary file fully into a buffer.
    // The path is interpreted relative to the current working directory
    // (usually the executable directory).
    std::vector<char> read_binary_file(const char* path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            std::println(stderr, "vk_pipeline_basic: failed to open SPIR-V file '{}'", path);
            return {};
        }

        const std::streamsize size = file.tellg();
        if (size <= 0) {
            std::println(stderr, "vk_pipeline_basic: SPIR-V file '{}' is empty", path);
            return {};
        }
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(static_cast<std::size_t>(size));
        if (!file.read(buffer.data(), size)) {
            std::println(stderr, "vk_pipeline_basic: failed to read SPIR-V file '{}'", path);
            return {};
        }
        return buffer;
    }

    // Create a VkShaderModule from SPIR-V bytes.
    VkShaderModule create_shader_module(VkDevice device, std::span<const char> code) {
        if (code.empty()) {
            return VK_NULL_HANDLE;
        }

        // SPIR-V must be 4-byte aligned and sized.
        if (code.size() % sizeof(std::uint32_t) != 0) {
            std::println(stderr, "vk_pipeline_basic: SPIR-V code size is not a multiple of 4");
            return VK_NULL_HANDLE;
        }

        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const std::uint32_t*>(code.data());

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
            std::println(stderr, "vk_pipeline_basic: vkCreateShaderModule failed");
            return VK_NULL_HANDLE;
        }

        return module;
    }

} // anonymous namespace

namespace strata::gfx::vk {
    BasicPipeline::~BasicPipeline() {
        if (!device) {
            return;
        }

        if (pipeline) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (layout) {
            vkDestroyPipelineLayout(device, layout, nullptr);
            layout = VK_NULL_HANDLE;
        }

        device = VK_NULL_HANDLE;
    }

    BasicPipeline::BasicPipeline(BasicPipeline&& other) noexcept
        : device(other.device)
        , layout(other.layout)
        , pipeline(other.pipeline) {
        other.device = VK_NULL_HANDLE;
        other.layout = VK_NULL_HANDLE;
        other.pipeline = VK_NULL_HANDLE;
    }

    BasicPipeline&
        BasicPipeline::operator=(BasicPipeline&& other) noexcept {
        if (this != &other) {
            if (device) {
                if (pipeline) {
                    vkDestroyPipeline(device, pipeline, nullptr);
                }
                if (layout) {
                    vkDestroyPipelineLayout(device, layout, nullptr);
                }
            }

            device = other.device;
            layout = other.layout;
            pipeline = other.pipeline;

            other.device = VK_NULL_HANDLE;
            other.layout = VK_NULL_HANDLE;
            other.pipeline = VK_NULL_HANDLE;
        }
        return *this;
    }

    BasicPipeline create_basic_pipeline(VkDevice device, VkFormat color_format) {
        BasicPipeline out{};
        out.device = device;

        if (!device) {
            std::println(stderr, "vk_pipeline_basic: device is null");
            return out;
        }

        // NOTE: These paths assume your shaders are copied to <exe>/shaders.
        // Adjust if you're still loading from ../../engine/gfx/shaders/...
        auto vert_bytes = read_binary_file("shaders/fullscreen_triangle.vert.spv");
        auto frag_bytes = read_binary_file("shaders/flat_color.frag.spv");

        if (vert_bytes.empty() || frag_bytes.empty()) {
            // read_binary_file already logged errors
            return out;
        }

        VkShaderModule vert_module = create_shader_module(device, vert_bytes);
        VkShaderModule frag_module = create_shader_module(device, frag_bytes);
        if (!vert_module || !frag_module) {
            if (vert_module) {
                vkDestroyShaderModule(device, vert_module, nullptr);
            }
            if (frag_module) {
                vkDestroyShaderModule(device, frag_module, nullptr);
            }
            return out;
        }

        VkPipelineShaderStageCreateInfo vert_stage{};
        vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vert_stage.module = vert_module;
        vert_stage.pName = "main";

        VkPipelineShaderStageCreateInfo frag_stage{};
        frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_stage.module = frag_module;
        frag_stage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = { vert_stage, frag_stage };

        // No vertex buffers: positions are generated in the vertex shader using gl_VertexIndex.
        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo input_asm{};
        input_asm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_asm.primitiveRestartEnable = VK_FALSE;

        // We will use dynamic viewport/scissor. Pipeline doesn't need static ones.
        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = nullptr;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = nullptr;

        VkDynamicState dyn_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(std::size(dyn_states));
        dynamic_state.pDynamicStates = dyn_states;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.depthClampEnable = VK_FALSE;
        raster.rasterizerDiscardEnable = VK_FALSE;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE; // depends on your VS winding
        raster.depthBiasEnable = VK_FALSE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        msaa.sampleShadingEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blend_attach{};
        blend_attach.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        blend_attach.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.logicOpEnable = VK_FALSE;
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attach;

        VkPipelineLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.pNext = nullptr;
        layout_ci.setLayoutCount = 0;
        layout_ci.pSetLayouts = nullptr;
        layout_ci.pushConstantRangeCount = 0;
        layout_ci.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &out.layout) != VK_SUCCESS) {
            std::println(stderr, "vk_pipeline_fullscreen: vkCreatePipelineLayout failed");
            vkDestroyShaderModule(device, vert_module, nullptr);
            vkDestroyShaderModule(device, frag_module, nullptr);
            out.layout = VK_NULL_HANDLE;
            return out;
        }

        // Dynamic rendering interface: specify the color attachment format.
        VkPipelineRenderingCreateInfo rendering_ci{};
        rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering_ci.colorAttachmentCount = 1;
        rendering_ci.pColorAttachmentFormats = &color_format;

        VkGraphicsPipelineCreateInfo gp_ci{};
        gp_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp_ci.pNext = &rendering_ci;
        gp_ci.stageCount = static_cast<std::uint32_t>(std::size(stages));
        gp_ci.pStages = stages;
        gp_ci.pVertexInputState = &vertex_input;
        gp_ci.pInputAssemblyState = &input_asm;
        gp_ci.pViewportState = &viewport_state;
        gp_ci.pRasterizationState = &raster;
        gp_ci.pMultisampleState = &msaa;
        gp_ci.pDepthStencilState = nullptr;    // no depth
        gp_ci.pColorBlendState = &blend;
        gp_ci.pDynamicState = &dynamic_state;
        gp_ci.layout = out.layout;
        gp_ci.renderPass = VK_NULL_HANDLE; // dynamic rendering
        gp_ci.subpass = 0;
        gp_ci.basePipelineHandle = VK_NULL_HANDLE;
        gp_ci.basePipelineIndex = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_ci, nullptr, &out.pipeline) != VK_SUCCESS) {
            std::println(stderr, "vk_pipeline_fullscreen: vkCreateGraphicsPipelines failed");
            vkDestroyPipelineLayout(device, out.layout, nullptr);
            out.layout = VK_NULL_HANDLE;
            out.pipeline = VK_NULL_HANDLE;
            vkDestroyShaderModule(device, vert_module, nullptr);
            vkDestroyShaderModule(device, frag_module, nullptr);
            return out;
        }

        // shader modules can be destroyed after pipeline creation
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);

        return out;
    }

} // namespace strata::gfx::vk
