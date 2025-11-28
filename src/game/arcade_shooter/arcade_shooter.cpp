#include "platform/window.h"
#include "gfx/rhi/gpu_device.h"
#include "gfx/renderer/render_2d.h"

using namespace strata::platform;
using namespace strata::gfx::rhi;
using namespace strata::gfx::renderer;

int main() {
    WindowDesc desc;
    desc.size  = { 1280, 720 };
    desc.title = "strata - renderer test";

    Window window{ desc };
    if (window.should_close()) {
        return 1;
    }

    auto wsi = window.native_wsi();

    DeviceCreateInfo create_info{};
    create_info.backend = BackendType::Vulkan;

    auto device = create_device(create_info, wsi);
    if (!device) {
        return 2;
    }

    auto [w, h] = window.framebuffer_size();
    SwapchainDesc sc_desc;
    sc_desc.size = { static_cast<std::uint32_t>(w),
                     static_cast<std::uint32_t>(h) };

    auto swapchain = device->create_swapchain(sc_desc, wsi);
    if (!swapchain) {
        return 3;
    }

    Render2D renderer{ *device, swapchain };

    while (!window.should_close()) {
        window.poll_events();

        auto [fw, fh] = window.framebuffer_size();
        Extent2D fb_size{
            static_cast<std::uint32_t>(fw),
            static_cast<std::uint32_t>(fh)
        };

        auto result = draw_frame_and_handle_resize(
            *device, swapchain, renderer, fb_size);

        if (result == FrameResult::Error) {
            break;
        }
    }

    return 0;
}
