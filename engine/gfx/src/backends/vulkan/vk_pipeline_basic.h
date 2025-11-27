// -----------------------------------------------------------------------------
// engine/gfx/src/backends/vulkan/vk_pipeline_basic.h
//
// Purpose:
//   Declares a minimal Vulkan graphics pipeline wrapper used to render a
//   fullscreen triangle via dynamic rendering.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>

// Forward declarations to avoid pulling Vulkan headers into dependent code.
// These mirror the canonical Vulkan definitions:
//   - Handles are opaque pointers to incomplete types.
//   - VkExtent2D is a simple width/height struct passed by value.
struct VkDevice_T;
struct VkPipelineLayout_T;
struct VkPipeline_T;

using VkDevice = VkDevice_T*;
using VkPipelineLayout = VkPipelineLayout_T*;
using VkPipeline = VkPipeline_T*;

struct VkExtent2D {
    std::uint32_t width;
    std::uint32_t height;
};

enum VkFormat;

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
