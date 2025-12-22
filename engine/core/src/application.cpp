// -----------------------------------------------------------------------------
// engine/core/src/application.cpp
//
// Purpose:
//   Placeholder source for the engine-level application wrapper. Intended to
//   house lifecycle management and high-level startup/shutdown code.
// -----------------------------------------------------------------------------

#include "strata/core/application.h"

#include <print>
#include <thread>

namespace strata::core
{

using clock = std::chrono::steady_clock;

static gfx::rhi::Extent2D clamp_framebuffer(std::int32_t width, std::int32_t height) noexcept
{
    return gfx::rhi::Extent2D{
        .width  = static_cast<std::uint32_t>(std::max(0, width)),
        .height = static_cast<std::uint32_t>(std::max(0, height)),
    };
}

struct Application::Impl
{
    ApplicationConfig config{};
    bool              exit_requested{false};

    platform::Window    window;
    platform::WsiHandle surface{};

    std::unique_ptr<gfx::rhi::IGpuDevice> device{};
    gfx::rhi::SwapchainHandle             swapchain{};

    gfx::renderer::Render2D renderer;

    std::uint64_t     frame_index{0};
    clock::time_point last_frame{};

    Impl(ApplicationConfig                       cfg,
         platform::Window&&                      window,
         platform::WsiHandle                     surface,
         std::unique_ptr<gfx::rhi::IGpuDevice>&& device,
         gfx::rhi::SwapchainHandle               swapchain,
         gfx::renderer::Render2D&&               render)
        : config(std::move(cfg)), window(std::move(window)), surface(surface),
          device(std::move(device)), swapchain(swapchain), renderer(std::move(render)),
          last_frame(clock::now())
    {
    }

    ~Impl() noexcept
    {
        // Critical: wait_idle runs BEFORE members are destroyed,
        // so Render2D::~Render2D can safely destroy pipelines.
        if (device)
        {
            device->wait_idle();
        }
    }
};

void Application::ImplDeleter::operator()(Impl* impl) const noexcept
{
    delete impl;
}

std::expected<Application, ApplicationError> Application::create(ApplicationConfig config)
{
    using gfx::rhi::SwapchainDesc;

    platform::Window window{config.window_desc};
    if (window.should_close())
    {
        return std::unexpected(ApplicationError::WindowCreateFailed);
    }

    auto surface = window.native_wsi();

    auto device = gfx::rhi::create_device(config.device, surface);
    if (!device)
    {
        return std::unexpected(ApplicationError::DeviceCreateFailed);
    }

    auto [fbw, fbh] = window.framebuffer_size();
    if (fbw <= 0 || fbh <= 0)
    {
        auto [ww, wh] = window.window_size();
        fbw           = (ww > 0) ? ww : config.window_desc.size.width;
        fbh           = (wh > 0) ? wh : config.window_desc.size.height;
    }

    SwapchainDesc sc_desc = config.swapchain_desc;
    sc_desc.size          = gfx::rhi::Extent2D{static_cast<std::uint32_t>(std::max(1, fbw)),
                                      static_cast<std::uint32_t>(std::max(1, fbh))};

    auto swapchain = device->create_swapchain(sc_desc, surface);
    if (!swapchain)
    {
        return std::unexpected(ApplicationError::SwapchainCreateFailed);
    }

    gfx::renderer::Render2D renderer{*device, swapchain};

    std::unique_ptr<Impl, ImplDeleter> impl{new Impl{std::move(config),
                                                     std::move(window),
                                                     surface,
                                                     std::move(device),
                                                     swapchain,
                                                     std::move(renderer)}};

    return Application{std::move(impl)};
}

void Application::request_exit() noexcept
{
    impl_->exit_requested = true;
    impl_->window.request_close();
}

std::int16_t Application::run(TickFn tick)
{
    using namespace std::chrono_literals;

    while (!impl_->exit_requested && !impl_->window.should_close())
    {
        impl_->window.poll_events();

        auto const                          now = clock::now();
        std::chrono::duration<double> const dt  = now - impl_->last_frame;
        impl_->last_frame                       = now;

        FrameContext ctx{};
        ctx.frame_index   = impl_->frame_index++;
        ctx.delta_seconds = dt.count();

        if (tick)
        {
            tick(*this, ctx);
        }

        auto [w, h]            = impl_->window.framebuffer_size();
        auto const framebuffer = clamp_framebuffer(w, h);

        // TODO: Why is 'draw_frame_and_handle_resize' not part of Render2D?
        auto result = gfx::renderer::draw_frame_and_handle_resize(*impl_->device,
                                                                  impl_->swapchain,
                                                                  impl_->renderer,
                                                                  framebuffer);

        if (result == gfx::rhi::FrameResult::Error)
        {
            std::println("Render error; exiting.");
            return 2;
        }

        if (impl_->config.throttle_cpu)
        {
            std::this_thread::sleep_for(impl_->config.throttle_sleep);
        }
    }

    impl_->device->wait_idle();
    return 0;
}

platform::Window& Application::window() noexcept
{
    return impl_->window;
}
platform::Window const& Application::window() const noexcept
{
    return impl_->window;
}

gfx::rhi::IGpuDevice& Application::device() noexcept
{
    return *impl_->device;
}
gfx::rhi::IGpuDevice const& Application::device() const noexcept
{
    return *impl_->device;
}

gfx::rhi::SwapchainHandle Application::swapchain() const noexcept
{
    return impl_->swapchain;
}

gfx::renderer::Render2D& Application::renderer() noexcept
{
    return impl_->renderer;
}
gfx::renderer::Render2D const& Application::renderer() const noexcept
{
    return impl_->renderer;
}

ApplicationConfig const& Application::config() const noexcept
{
    return impl_->config;
}

} // namespace strata::core