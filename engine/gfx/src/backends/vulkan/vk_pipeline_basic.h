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

// Handle-like Vulkan types are safe to forward declare
struct VkDevice_T;
struct VkPipeline_T;
struct VkPipelineLayout_T;

// Value types: declare the *tags* only, no fake typedefs:
struct VkExtent2D;  // matches Vulkan's "struct VkExtent2D { ... }"

// MSVC whines about this, so silence that one warning only:
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4471) // forward declaration of unscoped enum must have an underlying type
#endif
enum VkFormat;      // Vulkan defines: "typedef enum VkFormat { ... } VkFormat;"
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

using VkDevice = VkDevice_T*;
using VkPipeline = VkPipeline_T*;
using VkPipelineLayout = VkPipelineLayout_T*;

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
