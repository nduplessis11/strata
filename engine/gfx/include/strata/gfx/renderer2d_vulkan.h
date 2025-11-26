#pragma once

#include "strata/gfx/renderer2d.h"
#include "strata/gfx/vulkan/vulkan_context.h"
#include "strata/gfx/vulkan/swapchain.h"

#include <vector>

namespace strata::gfx {

class VulkanRenderContext final : public IRenderContext {
public:
        explicit VulkanRenderContext(const VulkanContext& ctx) : ctx_(ctx) {}

        Renderer2dNativeHandle device_handle() const override {
                return reinterpret_cast<Renderer2dNativeHandle>(ctx_.device());
        }
        std::uint32_t graphics_family_index() const override { return ctx_.graphics_family_index(); }
        Renderer2dNativeHandle graphics_queue_handle() const override {
                return reinterpret_cast<Renderer2dNativeHandle>(ctx_.graphics_queue());
        }
        Renderer2dNativeHandle present_queue_handle() const override {
                return reinterpret_cast<Renderer2dNativeHandle>(ctx_.present_queue());
        }

        [[nodiscard]] const VulkanContext& vulkan_context() const noexcept { return ctx_; }

private:
        const VulkanContext& ctx_;
};

class VulkanSwapchain final : public ISwapchain {
public:
        VulkanSwapchain(const VulkanContext& ctx, Swapchain& swapchain)
                : ctx_(ctx)
                , swapchain_(swapchain) {}

        bool valid() const override { return swapchain_.valid(); }
        Renderer2dNativeHandle handle() const override {
                return reinterpret_cast<Renderer2dNativeHandle>(swapchain_.handle());
        }
        Extent2d extent() const override { return swapchain_.extent(); }
        Renderer2dHandleSpan image_views() const override;
        Renderer2dHandleSpan images() const override;
        std::uint32_t color_format_bits() const override { return swapchain_.color_format_bits(); }

        bool recreate(Extent2d framebuffer_size) override;

        [[nodiscard]] Swapchain& swapchain() noexcept { return swapchain_; }
        [[nodiscard]] const Swapchain& swapchain() const noexcept { return swapchain_; }

private:
        const VulkanContext& ctx_;
        Swapchain& swapchain_;

        // Mutable because we repack Vulkan handles into generic handle spans on request.
        mutable std::vector<Renderer2dNativeHandle> image_view_handles_{};
        mutable std::vector<Renderer2dNativeHandle> image_handles_{};
};

struct VulkanRenderer2dDependencies : Renderer2dDependencies {
        VulkanRenderer2dDependencies(const VulkanContext& ctx, Swapchain& swapchain)
                : Renderer2dDependencies{ context_adapter, swapchain_adapter }
                , context_adapter(ctx)
                , swapchain_adapter(ctx, swapchain) {}

        VulkanRenderContext context_adapter;
        VulkanSwapchain swapchain_adapter;
};

// High-level frame tick specialized for the Vulkan backend.
[[nodiscard]] FrameResult draw_frame_and_handle_resize(Renderer2dDependencies& deps, Renderer2d& renderer, Extent2d framebuffer_size);

} // namespace strata::gfx
