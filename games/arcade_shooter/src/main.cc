#include "strata/platform/window.h"
#include "strata/gfx/vulkan/vulkan_context.h"
#include "strata/gfx/vulkan/wsi_bridge.h"
#include "strata/gfx/vulkan/swapchain.h"
#include "strata/gfx/renderer2d.h"

#include <chrono>
#include <thread>
#include <print>

int main() {
    using namespace strata::platform;
    using namespace strata::gfx;

    // Describe the window we want.
    WindowDesc desc;
    desc.size = { 1280, 720 };
    desc.title = "strata - Vulkan RAII test";

    Window win{ desc };
    if (win.should_close()) {
        std::println(stderr, "Failed to create window");
        return 1;
    }

    auto wsi = win.native_wsi();

    VulkanContextDesc ctx_desc{};
    VulkanContext ctx = VulkanContext::create(wsi, ctx_desc);
    if (!ctx.valid()) { std::println(stderr, "Failed to create Vulkan instance"); return 2; }
    if (!ctx.has_surface()) { std::println(stderr, "Failed to create Vulkan surface");  return 3; }
    if (!ctx.has_device()) { std::println(stderr, "Failed to create Vulkan device");   return 4; }

    auto [width, height] = win.framebuffer_size();
    Swapchain swapchain = Swapchain::create(ctx, Extent2d{ width, height });
    if (!swapchain.valid()) {
        std::println(stderr, "Failed to create initial swapchain");
        return 5;
    }

    Renderer2d renderer{ ctx, swapchain };

    // Main loop: pump events until the user closes the window.
    while (!win.should_close()) {
        win.poll_events();

        auto [w, h] { win.framebuffer_size() };
        FrameResult result{ draw_frame_and_handle_resize(ctx, swapchain, renderer, Extent2d{ w, h }) };

        if (result == FrameResult::Error) {
            // For now: bail. Later we might want more nuanced handling.
            break;
        }

        // Keep CPU reasonable for a light render loop.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
