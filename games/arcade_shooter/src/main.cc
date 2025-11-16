#include "strata/platform/window.h"
#include "strata/platform/wsi_handle.h"

#include <chrono>
#include <thread>
#include <cstdio>
#include <type_traits>

int main() {
    using namespace strata::platform;

    // Describe the window we want.
    WindowDesc desc;
    desc.size = { 1280, 720 };
    desc.title = "strata - window smoke";
    desc.visible = true;
    desc.resizable = true;

    // Create it (RAII). If creation failed, should_close() will be true.
    Window win{ desc };
    if (win.should_close()) {
        std::fprintf(stderr, "Failed to create window\n");
        return 1;
    }

    // Optional: show which WSI backend we got (Win32/X11/Wayland).
    const WsiHandle wsi = win.native_wsi();
    std::visit([](auto const& info) {
        using T = std::decay_t<decltype(info)>;
        if constexpr (std::is_same_v<T, wsi::Win32>) {
            std::printf("WSI: Win32 hwnd=%p hinstance=%p\n",
                reinterpret_cast<void*>(info.window.value),
                reinterpret_cast<void*>(info.instance.value));
        }
        else if constexpr (std::is_same_v<T, wsi::X11>) {
            std::printf("WSI: X11 display=%p window=%llu\n",
                reinterpret_cast<void*>(info.display.value),
                static_cast<unsigned long long>(info.window.value));
        }
        else { // Wayland
            std::printf("WSI: Wayland display=%p surface=%p\n",
                reinterpret_cast<void*>(info.display.value),
                reinterpret_cast<void*>(info.surface.value));
        }
        }, wsi);

    // Main loop: pump events until the user closes the window.
    while (!win.should_close()) {
        win.poll_events();

        // (Optional) print size changes
        static int last_w = -1, last_h = -1;
        auto [w, h] = win.window_size();
        if (w != last_w || h != last_h) {
            std::printf("client size: %dx%d\n", w, h);
            last_w = w; last_h = h;
        }

        // Keep CPU reasonable for a no-render loop.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
