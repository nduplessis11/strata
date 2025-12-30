// -----------------------------------------------------------------------------
// games/arcade_shooter/src/main.cpp
//
// Purpose:
//   Entry point for the arcade shooter sample application.
// -----------------------------------------------------------------------------

#include "strata/core/application.h"

#include <print>

int main()
{
    strata::core::ApplicationConfig cfg{};
    cfg.window_desc.size  = {1280, 720};
    cfg.window_desc.title = "Strata - Spinning Cube";

    cfg.device.backend       = strata::gfx::rhi::BackendType::Vulkan;
    cfg.swapchain_desc.vsync = true;

    cfg.throttle_cpu   = true;
    cfg.throttle_sleep = std::chrono::milliseconds{1};

    auto app = strata::core::Application::create(cfg);
    if (!app)
    {
        std::println("Failed to create Application: {}", strata::core::to_string(app.error()));
        return 1;
    }

    return app->run(
        [](strata::core::Application&, strata::core::FrameContext const&)
        {
            // Game tick goes here.
        });
}
