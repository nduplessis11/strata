#pragma once

#include "strata/gfx/renderer2d.h"
#include "strata/gfx/vulkan/vulkan_context.h"
#include "strata/gfx/vulkan/swapchain.h"

#include <cstdint>
#include <span>

namespace strata::gfx {

class IRenderContext {
public:
        virtual ~IRenderContext() = default;

        virtual VkDevice device() const = 0;
        virtual std::uint32_t graphics_family_index() const = 0;
        virtual VkQueue graphics_queue() const = 0;
        virtual VkQueue present_queue() const = 0;
};

class ISwapchain {
public:
        virtual ~ISwapchain() = default;

        virtual bool valid() const = 0;
        virtual VkSwapchainKHR handle() const = 0;
        virtual Extent2d extent() const = 0;
        virtual std::span<const VkImageView> image_views() const = 0;
        virtual std::span<const VkImage> images() const = 0;
        virtual std::uint32_t color_format_bits() const = 0;
        virtual bool recreate(Extent2d framebuffer_size) = 0;
};

struct Renderer2dDependencies {
        IRenderContext& context;
        ISwapchain& swapchain;
};

class VulkanRenderContext final : public IRenderContext {
public:
        explicit VulkanRenderContext(const VulkanContext& ctx) : ctx_(ctx) {}

        VkDevice device() const override { return ctx_.device(); }
        std::uint32_t graphics_family_index() const override { return ctx_.graphics_family_index(); }
        VkQueue graphics_queue() const override { return ctx_.graphics_queue(); }
        VkQueue present_queue() const override { return ctx_.present_queue(); }

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
        VkSwapchainKHR handle() const override { return swapchain_.handle(); }
        Extent2d extent() const override { return swapchain_.extent(); }
        std::span<const VkImageView> image_views() const override { return swapchain_.image_views(); }
        std::span<const VkImage> images() const override { return swapchain_.images(); }
        std::uint32_t color_format_bits() const override { return swapchain_.color_format_bits(); }

        bool recreate(Extent2d framebuffer_size) override;

        [[nodiscard]] Swapchain& swapchain() noexcept { return swapchain_; }
        [[nodiscard]] const Swapchain& swapchain() const noexcept { return swapchain_; }

private:
        const VulkanContext& ctx_;
        Swapchain& swapchain_;
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
