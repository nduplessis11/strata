// -----------------------------------------------------------------------------
// engine/platform/src/x11/window_x11.cpp
//
// Purpose:
//   X11 backend implementation for strata::platform::Window. Creates a basic
//   Xlib window, pumps events, and produces WSI handles for the graphics layer.
// -----------------------------------------------------------------------------

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string>
#include <utility>

#include "strata/platform/window.h"
#include "strata/platform/wsi_handle.h"

namespace strata::platform {

    struct Window::Impl {
        Display* display{};
        ::Window window{};
        Atom wm_delete{};
        bool closing{ false };
        bool visible{ false };
        bool minimized{ false };

        Impl(const WindowDesc& desc) {
            display = ::XOpenDisplay(nullptr);
            if (!display) {
                closing = true;
                return;
            }

            const int screen = DefaultScreen(display);
            const ::Window root = RootWindow(display, screen);

            XSetWindowAttributes attrs{};
            attrs.event_mask =
                ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
            attrs.background_pixel = BlackPixel(display, screen);

            const int border = 0;
            const unsigned int depth = CopyFromParent;
            window = ::XCreateWindow(
                display,
                root,
                0,
                0,
                static_cast<unsigned int>(desc.size.width),
                static_cast<unsigned int>(desc.size.height),
                static_cast<unsigned int>(border),
                depth,
                InputOutput,
                CopyFromParent,
                CWEventMask | CWBackPixel,
                &attrs);

            if (!window) {
                closing = true;
                return;
            }

            // Handle window close requests from the window manager (WM_DELETE_WINDOW)
            wm_delete = ::XInternAtom(display, "WM_DELETE_WINDOW", False);
            if (wm_delete) {
                ::XSetWMProtocols(display, window, &wm_delete, 1);
            }

            // Apply window hints (title, resizable flag via min/max size)
            std::string title{ desc.title };
            ::XStoreName(display, window, title.c_str());

            if (!desc.resizable) {
                XSizeHints hints{};
                hints.flags = PMinSize | PMaxSize;
                hints.min_width = hints.max_width = desc.size.width;
                hints.min_height = hints.max_height = desc.size.height;
                ::XSetWMNormalHints(display, window, &hints);
            }

            if (desc.visible) {
                ::XMapWindow(display, window);
                ::XFlush(display);
                visible = true;
            }
            else {
                visible = false;
            }
        }

        ~Impl() {
            if (display && window) {
                ::XDestroyWindow(display, window);
            }
            if (display) {
                ::XCloseDisplay(display);
            }
        }

        void request_close() noexcept {
            closing = true;
            if (display && window) {
                XEvent evt{};
                evt.xclient.type = ClientMessage;
                evt.xclient.message_type = wm_delete;
                evt.xclient.display = display;
                evt.xclient.window = window;
                evt.xclient.format = 32;
                evt.xclient.data.l[0] = static_cast<long>(wm_delete);
                evt.xclient.data.l[1] = CurrentTime;
                ::XSendEvent(display, window, False, NoEventMask, &evt);
                ::XFlush(display);
            }
        }

        void poll_events() {
            if (!display) return;

            while (::XPending(display) > 0) {
                XEvent evt{};
                ::XNextEvent(display, &evt);

                switch (evt.type) {
                case ClientMessage:
                    if (static_cast<Atom>(evt.xclient.data.l[0]) == wm_delete) {
                        closing = true;
                    }
                    break;
                case DestroyNotify:
                    closing = true;
                    break;
                case UnmapNotify:
                    visible = false;
                    minimized = true;
                    break;
                case MapNotify:
                    visible = true;
                    minimized = false;
                    break;
                default:
                    break;
                }
            }
        }

        std::pair<int, int> window_size() const noexcept {
            if (!display || !window) return { 0, 0 };

            XWindowAttributes attrs{};
            if (::XGetWindowAttributes(display, window, &attrs) == 0) {
                return { 0, 0 };
            }
            return { attrs.width, attrs.height };
        }

        auto make_wsi_handle() const -> WsiHandle {
            using namespace wsi;
            X11 h{};
            h.display.value = reinterpret_cast<std::uintptr_t>(display);
            h.window.value = static_cast<std::uint64_t>(window);
            return WsiHandle{ std::in_place_type<X11>, h };
        }
    };

    Window::Window(const WindowDesc& desc)
        : p_(std::make_unique<Impl>(desc)) {}

    Window::~Window() = default;

    Window::Window(Window&&) noexcept = default;
    Window& Window::operator=(Window&&) noexcept = default;

    bool Window::should_close() const noexcept {
        return p_->closing;
    }

    void Window::request_close() noexcept {
        if (p_) {
            p_->request_close();
        }
    }

    void Window::poll_events() {
        if (p_) {
            p_->poll_events();
        }
    }

    void Window::set_title(std::string_view title) {
        if (!p_ || !p_->display || !p_->window) return;
        std::string t{ title };
        ::XStoreName(p_->display, p_->window, t.c_str());
        ::XFlush(p_->display);
    }

    auto Window::window_size() const noexcept -> std::pair<int, int> {
        return p_ ? p_->window_size() : std::pair<int, int>{ 0, 0 };
    }

    auto Window::framebuffer_size() const noexcept -> std::pair<int, int> {
        return window_size();
    }

    bool Window::is_minimized() const noexcept {
        return p_ ? p_->minimized : true;
    }

    bool Window::is_visible() const noexcept {
        return p_ ? p_->visible : false;
    }

    WsiHandle Window::native_wsi() const noexcept {
        return p_ ? p_->make_wsi_handle() : WsiHandle{};
    }

} // namespace strata::platform
