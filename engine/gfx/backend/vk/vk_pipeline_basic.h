// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_pipeline_basic.h
//
// Purpose:
//   Declares a minimal Vulkan graphics pipeline wrapper used to render a
//   fullscreen triangle via dynamic rendering.
// -----------------------------------------------------------------------------

#pragma once

#include <span>

#include <vulkan/vulkan.h>

namespace strata::base
{
class Diagnostics;
}

namespace strata::gfx::vk
{

struct BasicPipeline
{
    VkDevice         device{};
    VkPipelineLayout layout{nullptr};
    VkPipeline       pipeline{nullptr};

    BasicPipeline()                                = default;
    BasicPipeline(BasicPipeline const&)            = delete;
    BasicPipeline& operator=(BasicPipeline const&) = delete;

    BasicPipeline(BasicPipeline&&) noexcept;
    BasicPipeline& operator=(BasicPipeline&&) noexcept;

    [[nodiscard]] bool valid() const noexcept
    {
        return device && layout && pipeline;
    }

    ~BasicPipeline();

    void destroy() noexcept;
};

// Default shader paths used by the v1 "basic pipeline".
// These match the historical hard-coded paths so behavior remains unchanged if
// callers don't provide shader paths explicitly.
inline constexpr char const* basic_pipeline_default_vertex_shader_path =
    "shaders/fullscreen_triangle.vert.spv";

inline constexpr char const* basic_pipeline_default_fragment_shader_path =
    "shaders/flat_color.frag.spv";

// Build a pipeline using Vulkan dynamic rendering.
// Returns an invalid BasicPipeline on failure.
//
// depth_format:
//   - VK_FORMAT_UNDEFINED => pipeline is created without depth attachment compatibility
//   - otherwise => pipeline is created compatible with a depth attachment of that format
//
// depth_test/depth_write
//  - Only meaningful if depth_format != VK_FORMAT_UNDEFINED
//
// vertex_shader_path / fragment_shader_path:
//  - If null/empty, defaults are used.
//
// vertex_bindings / vertex_attributes:
//  - If empty, pipeline is created with no vertex input (gl_VertexIndex style).
[[nodiscard]]
BasicPipeline create_basic_pipeline(
    VkDevice                               device,
    VkFormat                               color_format,
    ::strata::base::Diagnostics*           diag,
    std::span<VkDescriptorSetLayout const> set_layouts  = {},
    VkFormat                               depth_format = VK_FORMAT_UNDEFINED,
    bool                                   depth_test   = false,
    bool                                   depth_write  = false,
    char const* vertex_shader_path                      = basic_pipeline_default_vertex_shader_path,
    char const* fragment_shader_path = basic_pipeline_default_fragment_shader_path,
    std::span<VkVertexInputBindingDescription const>   vertex_bindings   = {},
    std::span<VkVertexInputAttributeDescription const> vertex_attributes = {});

} // namespace strata::gfx::vk
