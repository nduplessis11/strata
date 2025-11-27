// -----------------------------------------------------------------------------
// engine/gfx/src/backends/vulkan/vk_pipeline_basic.h
//
// Purpose:
//   Declares a minimal Vulkan graphics pipeline wrapper used to render a
//   fullscreen triangle via dynamic rendering.
// -----------------------------------------------------------------------------

#pragma once

#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace strata::gfx::vk {

    struct BasicPipeline {
        VkDevice         device{};
        VkPipelineLayout layout{ nullptr };
        VkPipeline       pipeline{ nullptr };

        BasicPipeline() = default;
        ~BasicPipeline();

        BasicPipeline(const BasicPipeline&) = delete;
        BasicPipeline& operator=(const BasicPipeline&) = delete;

        BasicPipeline(BasicPipeline&&) noexcept;
        BasicPipeline& operator=(BasicPipeline&&) noexcept;

        [[nodiscard]] bool valid() const noexcept {
            return device && layout && pipeline;
        }
    };

    // Build a pipeline that renders a fullscreen triangle using dynamic rendering.
    // Returns an invalid BasicPipeline on failure.
    [[nodiscard]] BasicPipeline create_basic_pipeline(VkDevice device, VkFormat color_format, VkExtent2D extent);

} // namespace strata::gfx::vk
