// -----------------------------------------------------------------------------
// engine/gfx/renderer/src/render_2d.cpp
//
// Purpose:
//   Implements the Render2D frontend on top of the RHI IGpuDevice interface.
//   Responsible for owning a basic graphics pipeline and cooperating with the
//   device for swapchain recreation on resize.
// -----------------------------------------------------------------------------

#include "strata/gfx/renderer/render_2d.h"

namespace strata::gfx::renderer
{

using namespace strata::gfx::rhi;

// -------------------------------------------------------------------------
// Render2D
// -------------------------------------------------------------------------

Render2D::Render2D(IGpuDevice& device, SwapchainHandle swapchain)
    : device_(&device), swapchain_(swapchain), pipeline_{}
{
    // Create pipeline
    PipelineDesc desc{};
    desc.vertex_shader_path   = "shaders/fullscreen_triangle.vert.spv";
    desc.fragment_shader_path = "shaders/flat_color.frag.spv";
    desc.alpha_blend          = false;

    pipeline_ = device_->create_pipeline(desc);
}

void Render2D::release() noexcept
{
    if (device_)
    {
        if (pipeline_)
        {
            device_->destroy_pipeline(pipeline_);
        }
    }

    // Clear handles regardless (safe for moved-from / partial init)
    pipeline_       = {};
    swapchain_      = {};
    device_         = nullptr;
}

Render2D::~Render2D()
{
    release();
}

Render2D::Render2D(Render2D&& other) noexcept
    : device_(other.device_), swapchain_(other.swapchain_), pipeline_(other.pipeline_)
{
    // moved-from becomes inert
    other.device_         = nullptr;
    other.swapchain_      = {};
    other.pipeline_       = {};
}

Render2D& Render2D::operator=(Render2D&& other) noexcept
{
    if (this != &other)
    {
        release();

        device_         = other.device_;
        swapchain_      = other.swapchain_;
        pipeline_       = other.pipeline_;

        other.device_         = nullptr;
        other.swapchain_      = {};
        other.pipeline_       = {};
    }
    return *this;
}

FrameResult Render2D::draw_frame()
{
    if (!device_ || !swapchain_ || !pipeline_)
        return FrameResult::Error;

    rhi::AcquiredImage img{};
    FrameResult const  acquire = device_->acquire_next_image(swapchain_, img);

    // If acquire says "Suboptimal", we should still render the frame,
    // but bubble up Suboptimal afterward so the caller can choose to resize.
    FrameResult hint = FrameResult::Ok;

    if (acquire == FrameResult::Error || acquire == FrameResult::ResizeNeeded)
    {
        return acquire;
    }
    if (acquire == FrameResult::Suboptimal)
    {
        hint = FrameResult::Suboptimal;
    }

    rhi::CommandBufferHandle const cmd = device_->begin_commands();
    if (!cmd)
        return FrameResult::Error;

    bool        pass_open = false;
    FrameResult result    = FrameResult::Error; // default unless we succeed

    rhi::ClearColor const clear{0.6f, 0.4f, 0.8f, 1.0f};

    // --- Record -------------------------------------------------------------
    if (device_->cmd_begin_swapchain_pass(cmd, swapchain_, img.image_index, clear) !=
        FrameResult::Ok)
    {
        goto cleanup;
    }
    pass_open = true;

    if (device_->cmd_bind_pipeline(cmd, pipeline_) != FrameResult::Ok)
    {
        goto cleanup;
    }

    if (device_->cmd_set_viewport_scissor(cmd, img.extent) != FrameResult::Ok)
    {
        goto cleanup;
    }

    if (device_->cmd_draw(cmd, 3, 1, 0, 0) != FrameResult::Ok)
    {
        goto cleanup;
    }

    if (device_->cmd_end_swapchain_pass(cmd, swapchain_, img.image_index) != FrameResult::Ok)
    {
        goto cleanup;
    }
    pass_open = false;

    if (device_->end_commands(cmd) != FrameResult::Ok)
    {
        result = FrameResult::Error;
        goto cleanup_after_end; // don't call end_commands() again
    }

    // --- Submit -------------------------------------------------------------
    {
        rhi::IGpuDevice::SubmitDesc sd{};
        sd.command_buffer = cmd;
        sd.swapchain      = swapchain_;
        sd.image_index    = img.image_index;
        sd.frame_index    = img.frame_index;

        FrameResult const sub = device_->submit(sd);
        if (sub != FrameResult::Ok)
        {
            result = sub;
            goto cleanup_after_end; // command buffer already ended
        }
    }

    // --- Present ------------------------------------------------------------
    {
        FrameResult const pres = device_->present(swapchain_, img.image_index);
        if (pres == FrameResult::Ok)
        {
            // If acquire was Suboptimal, bubble it up so caller can decide to resize.
            result = hint;
        }
        else
        {
            result = pres;
        }
        return result;
    }

cleanup:
    // Best-effort: close rendering if we opened it.
    if (pass_open)
    {
        device_->cmd_end_swapchain_pass(cmd, swapchain_, img.image_index);
        pass_open = false;
    }

    // Best-effort: end the command buffer so it can be reset next frame.
    device_->end_commands(cmd);

cleanup_after_end:
    return result;
}

// -------------------------------------------------------------------------
// Helper: draw_frame_and_handle_resize
// -------------------------------------------------------------------------

FrameResult draw_frame_and_handle_resize(IGpuDevice&      device,
                                         SwapchainHandle& swapchain,
                                         Render2D&        renderer,
                                         Extent2D         framebuffer_size)
{

    // Minimized / zero-area window: skip rendering but don't treat as error.
    if (framebuffer_size.width == 0 || framebuffer_size.height == 0)
    {
        return FrameResult::Ok;
    }

    FrameResult const result = renderer.draw_frame();
    if (result == FrameResult::Ok || result == FrameResult::Error)
        return result;

    // Any non-Ok, non-Error result is treated as "swapchain needs resize".
    device.wait_idle();

    SwapchainDesc sc_desc{};
    sc_desc.size  = framebuffer_size;
    sc_desc.vsync = true; // or expose as parameter later

    // Resize existing swapchain in-place.
    FrameResult const resize_result = device.resize_swapchain(swapchain, sc_desc);
    if (resize_result == FrameResult::Error)
    {
        // Failed to resize; treat as non-fatal (no frame rendered).
        return FrameResult::Ok;
    }

    // Rebuild the pipeline via a fresh Render2D for the resized swapchain.
    renderer = Render2D{device, swapchain};

    return FrameResult::Ok;
}

} // namespace strata::gfx::renderer
