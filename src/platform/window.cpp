#include "platform/window.h"

#include <utility>

namespace strata::platform {
    struct Window::Impl {
        WindowDesc desc{};
        bool should_close{ false };
        WsiHandle wsi{};
    };

    Window::Window(const WindowDesc& desc)
        : p_(std::make_unique<Impl>())
    {
        p_->desc = desc;
    }

    Window::~Window() = default;
    Window::Window(Window&&) noexcept = default;
    Window& Window::operator=(Window&&) noexcept = default;

    bool Window::should_close() const noexcept { return p_->should_close; }
    void Window::request_close() noexcept { p_->should_close = true; }

    void Window::poll_events() { /* platform event pump goes here */ }
    void Window::set_title(std::string_view title) { p_->desc.title = title; }

    auto Window::window_size() const noexcept -> std::pair<int, int> {
        return { p_->desc.size.width, p_->desc.size.height };
    }

    auto Window::framebuffer_size() const noexcept -> std::pair<int, int> {
        return window_size();
    }

    bool Window::is_minimized() const noexcept { return false; }
    bool Window::is_visible() const noexcept { return p_->desc.visible; }

    auto Window::native_wsi() const noexcept -> WsiHandle { return p_->wsi; }

} // namespace strata::platform
