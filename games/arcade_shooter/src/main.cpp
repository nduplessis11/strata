// -----------------------------------------------------------------------------
// games/arcade_shooter/src/main.cc
//
// Purpose:
//   Entry point for the arcade_shooter sample. Bootstraps the platform window,
//   initializes the Vulkan RHI device, and drives the Render2D loop.
// -----------------------------------------------------------------------------

#include "strata/platform/window.h"
#include "gfx/rhi/gpu_device.h"
#include "gfx/renderer/render_2d.h"

#include <chrono>
#include <print>
#include <thread>

int main() {
    using namespace std::chrono_literals;

    using strata::platform::Extent2d;
    using strata::platform::Window;
    using strata::platform::WindowDesc;

    using strata::gfx::rhi::BackendType;
    using strata::gfx::rhi::DeviceCreateInfo;
    using strata::gfx::rhi::Extent2D;
    using strata::gfx::rhi::FrameResult;
    using strata::gfx::rhi::SwapchainDesc;
    using strata::gfx::rhi::SwapchainHandle;

    // --- Create window --------------------------------------------------------
    WindowDesc desc;
    desc.size = { 1280, 720 };
    desc.title = "strata - RHI renderer test";

    Window win{ desc };
    if (win.should_close()) {
        return 0;
    }

    auto surface = win.native_wsi();

    // --- Create RHI device ----------------------------------------------------
    DeviceCreateInfo dev_info{};
    dev_info.backend = BackendType::Vulkan;

    auto device = strata::gfx::rhi::create_device(dev_info, surface);
    if (!device) {
        std::println("Failed to create RHI device.");
        return 1;
    }

    // --- Create swapchain -----------------------------------------------------
    auto [fbw, fbh] = win.framebuffer_size();
    SwapchainDesc sc_desc{};
    sc_desc.size = Extent2D{
        static_cast<std::uint32_t>(fbw),
        static_cast<std::uint32_t>(fbh)
    };
    // sc_desc.format and sc_desc.vsync use defaults from SwapchainDesc

    SwapchainHandle swapchain = device->create_swapchain(sc_desc, surface);
    if (!swapchain) {
        std::println("Failed to create swapchain.");
        return 1;
    }

    // --- Create renderer frontend --------------------------------------------
    strata::gfx::renderer::Render2D renderer{ *device, swapchain };

    // --- Main loop ------------------------------------------------------------
    while (!win.should_close()) {
        win.poll_events();

        auto [w, h] = win.framebuffer_size();
        Extent2D fb_size{
            static_cast<std::uint32_t>(w),
            static_cast<std::uint32_t>(h)
        };

        FrameResult result =
            strata::gfx::renderer::draw_frame_and_handle_resize(
                *device,
                swapchain,
                renderer,
                fb_size);

        if (result == FrameResult::Error) {
            std::println("Render error; exiting.");
            break;
        }

        // Small throttle so we don't peg the CPU in this tiny sample.
        std::this_thread::sleep_for(1ms);
    }

    device->wait_idle();
    return 0;
}
