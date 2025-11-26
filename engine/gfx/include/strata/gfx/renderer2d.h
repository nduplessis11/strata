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

#include <cstdint>
#include <memory>
#include <span>

#include "strata/platform/window.h"

namespace strata::gfx {

using Renderer2dNativeHandle = std::uintptr_t;
using Renderer2dHandleSpan = std::span<const Renderer2dNativeHandle>;

class IRenderContext {
public:
    virtual ~IRenderContext() = default;

    virtual Renderer2dNativeHandle device_handle() const = 0;
    virtual std::uint32_t graphics_family_index() const = 0;
    virtual Renderer2dNativeHandle graphics_queue_handle() const = 0;
    virtual Renderer2dNativeHandle present_queue_handle() const = 0;
};

class ISwapchain {
public:
    virtual ~ISwapchain() = default;

    virtual bool valid() const = 0;
    virtual Renderer2dNativeHandle handle() const = 0;
    virtual Extent2d extent() const = 0;
    virtual Renderer2dHandleSpan image_views() const = 0;
    virtual Renderer2dHandleSpan images() const = 0;
    virtual std::uint32_t color_format_bits() const = 0;
    virtual bool recreate(Extent2d framebuffer_size) = 0;
};

struct Renderer2dDependencies {
    IRenderContext& context;
    ISwapchain& swapchain;
};

enum class FrameResult {
    Ok,                 // frame rendered & presented
    SwapchainOutOfDate, // need to recreate swapchain (resize, etc.)
    Error               // unrecoverable for now
};

class Renderer2d {
public:
    // Construct a renderer bound to an existing VulkanContext + Swapchain.
    // Both ctx and swapchain must outlive this Renderer2d.
    explicit Renderer2d(const Renderer2dDependencies& deps);
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

} // namespace strata::gfx
