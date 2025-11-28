#include "platform/window.h"

#include <utility>

namespace strata::platform {
    struct Window::Impl;

    namespace detail {
        Window::Impl* create_window_impl(const WindowDesc& desc);
        void          destroy_window_impl(Window::Impl* impl) noexcept;

        bool should_close_impl(const Window::Impl& impl) noexcept;
        void request_close_impl(Window::Impl& impl) noexcept;

        void poll_events_impl(Window::Impl& impl);
        void set_title_impl(Window::Impl& impl, std::string_view title);

        auto window_size_impl(const Window::Impl& impl) noexcept
            -> std::pair<int, int>;
        auto framebuffer_size_impl(const Window::Impl& impl) noexcept
            -> std::pair<int, int>;

        bool is_minimized_impl(const Window::Impl& impl) noexcept;
        bool is_visible_impl(const Window::Impl& impl) noexcept;

        auto native_wsi_impl(const Window::Impl& impl) noexcept -> WsiHandle;
    } // namespace detail

    Window::Window(const WindowDesc& desc)
        : p_(detail::create_window_impl(desc), detail::destroy_window_impl)
    {}

    Window::~Window() = default;
    Window::Window(Window&&) noexcept = default;
    Window& Window::operator=(Window&&) noexcept = default;

    bool Window::should_close() const noexcept {
        return p_ ? detail::should_close_impl(*p_) : true;
    }

    void Window::request_close() noexcept {
        if (p_) detail::request_close_impl(*p_);
    }

    void Window::poll_events() {
        if (p_) detail::poll_events_impl(*p_);
    }

    void Window::set_title(std::string_view title) {
        if (p_) detail::set_title_impl(*p_, title);
    }

    auto Window::window_size() const noexcept -> std::pair<int, int> {
        if (!p_) return { 0, 0 };
        return detail::window_size_impl(*p_);
    }

    auto Window::framebuffer_size() const noexcept -> std::pair<int, int> {
        if (!p_) return { 0, 0 };
        return detail::framebuffer_size_impl(*p_);
    }

    bool Window::is_minimized() const noexcept {
        return p_ ? detail::is_minimized_impl(*p_) : true;
    }

    bool Window::is_visible() const noexcept {
        return p_ ? detail::is_visible_impl(*p_) : false;
    }

    auto Window::native_wsi() const noexcept -> WsiHandle {
        if (!p_) return {};
        return detail::native_wsi_impl(*p_);
    }

#if !defined(_WIN32)
    struct Window::Impl {
        WindowDesc desc{};
        bool       should_close{ false };
        WsiHandle  wsi{};
    };

    Window::Impl* detail::create_window_impl(const WindowDesc& desc) {
        auto* impl = new Window::Impl{};
        impl->desc = desc;
        impl->wsi = {};
        return impl;
    }

    void detail::destroy_window_impl(Window::Impl* impl) noexcept {
        delete impl;
    }

    bool detail::should_close_impl(const Window::Impl& impl) noexcept {
        return impl.should_close;
    }

    void detail::request_close_impl(Window::Impl& impl) noexcept {
        impl.should_close = true;
    }

    void detail::poll_events_impl(Window::Impl&) {
        // No-op stub for non-Windows platforms in this experimental stack.
    }

    void detail::set_title_impl(Window::Impl& impl, std::string_view title) {
        impl.desc.title = title;
    }

    auto detail::window_size_impl(const Window::Impl& impl) noexcept
        -> std::pair<int, int> {
        return { impl.desc.size.width, impl.desc.size.height };
    }

    auto detail::framebuffer_size_impl(const Window::Impl& impl) noexcept
        -> std::pair<int, int> {
        return window_size_impl(impl);
    }

    bool detail::is_minimized_impl(const Window::Impl&) noexcept {
        return false;
    }

    bool detail::is_visible_impl(const Window::Impl& impl) noexcept {
        return impl.desc.visible;
    }

    auto detail::native_wsi_impl(const Window::Impl& impl) noexcept -> WsiHandle {
        return impl.wsi;
    }
#endif

} // namespace strata::platform
