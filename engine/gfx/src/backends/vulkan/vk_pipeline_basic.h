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
//   - VkExtent2D is declared but defined by the Vulkan headers; we only need a reference.
struct VkDevice_T;
struct VkPipelineLayout_T;
struct VkPipeline_T;

using VkDevice = VkDevice_T*;
using VkPipelineLayout = VkPipelineLayout_T*;
using VkPipeline = VkPipeline_T*;

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
    [[nodiscard]] BasicPipeline create_basic_pipeline(VkDevice device, VkFormat color_format, const VkExtent2D& extent);

} // namespace strata::gfx::vk
