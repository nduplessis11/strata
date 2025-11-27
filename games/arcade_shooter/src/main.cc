// -----------------------------------------------------------------------------
// games/arcade_shooter/src/main.cc
//
// Purpose:
//   Entry point for the arcade_shooter sample. Bootstraps the platform window,
//   initializes the Vulkan graphics device, and drives the Renderer2d loop.
// -----------------------------------------------------------------------------

#include "strata/platform/window.h"
#include "strata/gfx/graphics_device.h"
#include "strata/gfx/renderer2d.h"

#include <chrono>
#include <print>
#include <thread>

int main() {
    using namespace strata::platform;
    using namespace strata::gfx;

    WindowDesc desc;
    desc.size = { 1280, 720 };
    desc.title = "strata - renderer test";

    Window win{ desc };
    if (win.should_close()) {
        std::println(stderr, "Failed to create window");
        return 1;
    }

    auto wsi = win.native_wsi();

    auto device = GraphicsDevice::create(BackendType::Vulkan, wsi);
    if (!device) {
        std::println(stderr, "Failed to create graphics device");
        return 2;
    }

    auto [width, height] = win.framebuffer_size();
    auto swapchain = device->create_swapchain(Extent2d{ width, height });
    if (!swapchain || !swapchain->is_valid()) {
        std::println(stderr, "Failed to create initial swapchain");
        return 3;
    }

    Renderer2d renderer{ *device, *swapchain };

    while (!win.should_close()) {
        win.poll_events();

        auto [w, h] = win.framebuffer_size();
        FrameResult result{ draw_frame_and_handle_resize(*device, swapchain, renderer, Extent2d{ w, h }) };

        if (result == FrameResult::Error) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
