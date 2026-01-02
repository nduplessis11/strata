// -----------------------------------------------------------------------------
// engine/platform/src/x11/window_x11.cpp
//
// Purpose:
//   X11 backend implementation for strata::platform::Window. Creates a basic
//   Xlib window, pumps events, and produces WSI handles for the graphics layer.
//
// V1 Camera Input:
//   - Track raw input state (keys, mouse buttons, mouse delta) per Window.
//   - Reset per-frame mouse delta in poll_events().
// -----------------------------------------------------------------------------

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysim.h>

#include <string>
#include <utility>

#include "strata/base/diagnostics.h"
#include "strata/platform/input.h"
#include "strata/platform/window.h"
#include "strata/platform/wsi_handle.h"

namespace
{

bool translate_key(KeySym sym, strata::platform::Key& out) noexcept
{
    using strata::platform::Key;

    switch (sym)
    {
    case XK_w:
    case XK_W:
        out = Key::W;
        return true;
    case XK_a:
    case XK_A:
        out = Key::A;
        return true;
    case XK_s:
    case XK_S:
        out = Key::S;
        return true;
    case XK_d:
    case XK_D:
        out = Key::D;
        return true;

    case XK_space:
        out = Key::Space;
        return true;

    case XK_Control_L:
    case XK_Control_R:
        out = Key::Ctrl;
        return true;

    case XK_Shift_L:
    case XK_Shift_R:
        out = Key::Shift;
        return true;

    case XK_Escape:
        out = Key::Escape;
        return true;

    default:
        return false;
    }
}

bool translate_button(unsigned int button, strata::platform::MouseButton& out) noexcept
{
    using strata::platform::MouseButton;

    switch (button)
    {
    case Button1:
        out = MouseButton::Left;
        return true;
    case Button2:
        out = MouseButton::Middle;
        return true;
    case Button3:
        out = MouseButton::Right;
        return true;
    default:
        return false;
    }
}

} // namespace

namespace strata::platform
{

struct Window::Impl
{
    base::Diagnostics* diagnostics{nullptr};

    Display* display{};
    ::Window window{};
    Atom     wm_delete{};
    bool     closing{false};
    bool     visible{false};
    bool     minimized{false};

    // V1 Camera Input: input state owned by this Window.
    InputState input{};

    bool mouse_pos_valid{false};
    int  last_mouse_x{0};
    int  last_mouse_y{0};

    Impl(base::Diagnostics& diag, WindowDesc const& desc)
          : diagnostics(&diag)
    {
        display = ::XOpenDisplay(nullptr);
        if (!display)
        {
            STRATA_LOG_ERROR(diagnostics->logger(), "platform", "X11: XOpenDisplay failed");
            closing = true;
            return;
        }

        Bool supported = False;
        XkbSetDetectableAutoRepeat(display, True, &supported);

        int const      screen = DefaultScreen(display);
        ::Window const root   = RootWindow(display, screen);

        XSetWindowAttributes attrs{};
        attrs.event_mask = ExposureMask |
            StructureNotifyMask |
            FocusChangeMask |
            KeyPressMask |
            KeyReleaseMask |
            ButtonPressMask |
            ButtonReleaseMask |
            PointerMotionMask;
        attrs.background_pixel = BlackPixel(display, screen);

        int const          border = 0;
        unsigned int const depth  = CopyFromParent;
        window                    = ::XCreateWindow(display,
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

        if (!window)
        {
            STRATA_LOG_ERROR(diagnostics->logger(), "platform", "X11: XCreateWindow failed");
            closing = true;
            return;
        }

        // Handle window close requests from the window manager (WM_DELETE_WINDOW)
        wm_delete = ::XInternAtom(display, "WM_DELETE_WINDOW", False);
        if (wm_delete)
        {
            ::XSetWMProtocols(display, window, &wm_delete, 1);
        }

        // Apply window hints (title, resizable flag via min/max size)
        std::string title{desc.title};
        ::XStoreName(display, window, title.c_str());

        if (!desc.resizable)
        {
            XSizeHints hints{};
            hints.flags     = PMinSize | PMaxSize;
            hints.min_width = hints.max_width = desc.size.width;
            hints.min_height = hints.max_height = desc.size.height;
            ::XSetWMNormalHints(display, window, &hints);
        }

        if (desc.visible)
        {
            ::XMapWindow(display, window);
            ::XFlush(display);
            visible = true;
        }
        else
        {
            visible = false;
        }
    }

    ~Impl()
    {
        if (display && window)
        {
            ::XDestroyWindow(display, window);
        }
        if (display)
        {
            ::XCloseDisplay(display);
        }
    }

    void request_close() noexcept
    {
        closing = true;
        if (display && window)
        {
            XEvent evt{};
            evt.xclient.type         = ClientMessage;
            evt.xclient.message_type = wm_delete;
            evt.xclient.display      = display;
            evt.xclient.window       = window;
            evt.xclient.format       = 32;
            evt.xclient.data.l[0]    = static_cast<long>(wm_delete);
            evt.xclient.data.l[1]    = CurrentTime;
            ::XSendEvent(display, window, False, NoEventMask, &evt);
            ::XFlush(display);
        }
    }

    void poll_events()
    {
        if (!display)
            return;

        // V1 Camera Input: reset per-frame deltas before pumping.
        input.begin_frame();

        while (::XPending(display) > 0)
        {
            XEvent evt{};
            ::XNextEvent(display, &evt);

            switch (evt.type)
            {
            case FocusIn:
                input.set_focused(true);
                mouse_pos_valid = false;
                break;

            case FocusOut:
                input.set_focused(false);
                mouse_pos_valid = false;
                break;

            case KeyPress:
            {
                KeySym sym = ::XLookupKeysym(&evt.xkey, 0);
                Key    k{};
                if (translate_key(sym, k))
                {
                    input.set_key(k, true);
                }
                break;
            }

            case KeyRelease:
            {
                // Filter X11 key auto-repeat:
                // Auto-repeat generates KeyRelease/KeyPress pairs with the same keycode+time.
                if (XEventsQueued(display, QueuedAfterReading) > 0)
                {
                    XEvent next{};
                    XPeekEvent(display, &next);

                    if (next.type == KeyPress &&
                        next.xkey.keycode == evt.xkey.keycode &&
                        next.xkey.time == evt.xkey.time)
                    {
                        // This release is part of auto-repeat; ignore it.
                        break;
                    }
                }

                KeySym sym = ::XLookupKeysym(&evt.xkey, 0);
                Key    k{};
                if (translate_key(sym, k))
                    input.set_key(k, false);

                break;
            }

            case ButtonPress:
            {
                // Wheel buttons are typically 4/5; treat as wheel delta.
                if (evt.xbutton.button == Button4)
                {
                    input.add_wheel_delta(+1.0f);
                    break;
                }
                if (evt.xbutton.button == Button5)
                {
                    input.add_wheel_delta(-1.0f);
                    break;
                }

                MouseButton b{};
                if (translate_button(evt.xbutton.button, b))
                {
                    input.set_mouse_button(b, true);
                }
                break;
            }

            case ButtonRelease:
            {
                MouseButton b{};
                if (translate_button(evt.xbutton.button, b))
                {
                    input.set_mouse_button(b, false);
                }
                break;
            }

            case MotionNotify:
            {
                if (!input.focused())
                    break;

                int const x = evt.xmotion.x;
                int const y = evt.xmotion.y;

                if (mouse_pos_valid)
                {
                    int const dx = x - last_mouse_x;
                    int const dy = y - last_mouse_y;
                    input.add_mouse_delta(static_cast<float>(dx), static_cast<float>(dy));
                }

                last_mouse_x    = x;
                last_mouse_y    = y;
                mouse_pos_valid = true;
                break;
            }

            case ClientMessage:
                if (static_cast<Atom>(evt.xclient.data.l[0]) == wm_delete)
                {
                    closing = true;
                }
                break;

            case DestroyNotify:
                closing = true;
                break;

            case UnmapNotify:
                visible   = false;
                minimized = true;
                break;

            case MapNotify:
                visible   = true;
                minimized = false;
                break;

            default:
                break;
            }
        }
    }

    std::pair<int, int> window_size() const noexcept
    {
        if (!display || !window)
            return {0, 0};

        XWindowAttributes attrs{};
        if (::XGetWindowAttributes(display, window, &attrs) == 0)
        {
            return {0, 0};
        }
        return {attrs.width, attrs.height};
    }

    auto make_wsi_handle() const -> WsiHandle
    {
        using namespace wsi;
        X11 h{};
        h.display.value = reinterpret_cast<std::uintptr_t>(display);
        h.window.value  = static_cast<std::uint64_t>(window);
        return WsiHandle{std::in_place_type<X11>, h};
    }
};

Window::Window(base::Diagnostics& diagnostics, WindowDesc const& desc)
      : p_(std::make_unique<Impl>(diagnostics, desc))
{
}

Window::~Window() = default;

Window::Window(Window&&) noexcept            = default;
Window& Window::operator=(Window&&) noexcept = default;

bool Window::should_close() const noexcept
{
    return p_->closing;
}

void Window::request_close() noexcept
{
    if (p_)
    {
        p_->request_close();
    }
}

void Window::poll_events()
{
    if (p_)
    {
        p_->poll_events();
    }
}

void Window::set_title(std::string_view title)
{
    if (!p_ || !p_->display || !p_->window)
        return;
    std::string t{title};
    ::XStoreName(p_->display, p_->window, t.c_str());
    ::XFlush(p_->display);
}

auto Window::window_size() const noexcept -> std::pair<int, int>
{
    return p_ ? p_->window_size() : std::pair<int, int>{0, 0};
}

auto Window::framebuffer_size() const noexcept -> std::pair<int, int>
{
    return window_size();
}

bool Window::is_minimized() const noexcept
{
    return p_ ? p_->minimized : true;
}

bool Window::is_visible() const noexcept
{
    return p_ ? p_->visible : false;
}

InputState const& Window::input() const noexcept
{
    return p_->input;
}

WsiHandle Window::native_wsi() const noexcept
{
    return p_ ? p_->make_wsi_handle() : WsiHandle{};
}

} // namespace strata::platform
