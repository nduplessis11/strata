// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer2d.h
//
// Purpose:
//   High-level 2D renderer front-end. This class provides a minimal interface
//   for issuing a frame (draw_frame) without exposing Vulkan objects or
//   lifetime details in the public header.
//
// Design Notes:
//   • pImpl:
//       Renderer2d owns a std::unique_ptr<Impl>. All Vulkan handles
//       (command pool, command buffer, semaphores, fence) live in Impl,
//       defined in renderer2d.cc. This keeps compile-time dependencies low
//       and the ABI stable.
//   • Lifetime:
//       Renderer2d does *not* own the VulkanContext or Swapchain; it holds
//       non-owning pointers to them. They must outlive the renderer.
//   • Rule of Zero in the header:
//       The only special members we declare are the move operations; the
//       destructor and everything else are defaulted in the source.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>
#include "strata/gfx/vulkan/vulkan_context.h"
#include "strata/gfx/vulkan/swapchain.h"

namespace strata::gfx {

    enum class FrameResult {
        Ok,                 // frame rendered & presented
        SwapchainOutOfDate, // need to recreate swapchain (resize, etc.)
        Error               // unrecoverable for now
    };

    class Renderer2d {
    public:
        // Construct a renderer bound to an existing VulkanContext + Swapchain.
        // Both ctx and swapchain must outlive this Renderer2d.
        Renderer2d(const VulkanContext& ctx, const Swapchain& swapchain);
        ~Renderer2d();

        Renderer2d(Renderer2d&&) noexcept;
        Renderer2d& operator=(Renderer2d&&) noexcept;

        Renderer2d(const Renderer2d&) = delete;
        Renderer2d& operator=(const Renderer2d&) = delete;

        // Issue one frame: acquire, render, present.
        // Returns a status so callers can react (e.g., recreate swapchain).
        [[nodiscard]] FrameResult draw_frame();

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };

    // High-level frame tick:
    //  - draws one frame
    //  - if swapchain is out-of-date, recreates it (and the renderer)
    //  - gracefully skips rendering when the window is minimized
    //
    // Returns:
    //   FrameResult::Ok    – frame was rendered or safely skipped
    //   FrameResult::Error – unrecoverable error, caller should bail
    [[nodiscard]] FrameResult draw_frame_and_handle_resize(const VulkanContext& ctx, Swapchain& swapchain, Renderer2d& renderer, Extent2d framebuffer_size);

} // namespace strata::gfx
