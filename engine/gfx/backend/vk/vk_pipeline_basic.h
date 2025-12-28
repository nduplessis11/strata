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

// Build a pipeline that renders a fullscreen triangle using dynamic rendering.
// Returns an invalid BasicPipeline on failure.
//
// depth_format:
//   - VK_FORMAT_UNDEFINED => pipeline is created without depth attachment compatibility
//   - otherwise => pipeline is created compatible with a depth attachment of that format
//
// depth_test/depth_write
//  - Only meaningful if depth_format != VK_FORMAT_UNDEFINED
[[nodiscard]]
BasicPipeline create_basic_pipeline(VkDevice                               device,
                                    VkFormat                               color_format,
                                    std::span<VkDescriptorSetLayout const> set_layouts = {},
                                    VkFormat                     depth_format = VK_FORMAT_UNDEFINED,
                                    bool                         depth_test   = false,
                                    bool                         depth_write  = false,
                                    ::strata::base::Diagnostics* diag);

} // namespace strata::gfx::vk
