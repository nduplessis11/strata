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
//
// Cursor control (CursorMode):
//   - Normal:   visible, not confined
//   - Hidden:   hidden, not confined
//   - Confined: visible, confined while focused (XGrabPointer with confine_to=window)
//   - Locked:   hidden + confined; additionally warp-to-center for endless deltas
// -----------------------------------------------------------------------------

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

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

    // V1 Camera Input
    InputState input{};

    bool mouse_pos_valid{false};
    int  last_mouse_x{0};
    int  last_mouse_y{0};

    // Cursor control
    CursorMode cursor_mode{CursorMode::Normal};
    Cursor     invisible_cursor{};
    bool       invisible_cursor_ready{false};
    bool       pointer_grabbed{false};
    bool       ignore_next_motion{false};

    int cached_w{0};
    int cached_h{0};

    void ensure_invisible_cursor() noexcept
    {
        if (!display || !window || invisible_cursor_ready)
            return;

        // 8x8 empty bitmap cursor
        static char const no_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};

        Pixmap bm = ::XCreateBitmapFromData(display, window, no_data, 8, 8);
        if (!bm)
            return;

        XColor black{};
        black.red = black.green = black.blue = 0;

        invisible_cursor = ::XCreatePixmapCursor(display, bm, bm, &black, &black, 0, 0);
        ::XFreePixmap(display, bm);

        invisible_cursor_ready = (invisible_cursor != 0);
    }

    void ungrab_pointer() noexcept
    {
        if (display && pointer_grabbed)
        {
            ::XUngrabPointer(display, CurrentTime);
            pointer_grabbed = false;
        }
    }

    bool grab_pointer(bool confine, Cursor cursor_shape) noexcept
    {
        if (!display || !window)
            return false;

        int const r = ::XGrabPointer(display,
                                     window,
                                     True,
                                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                     GrabModeAsync,
                                     GrabModeAsync,
                                     confine ? window : None,
                                     cursor_shape,
                                     CurrentTime);

        if (r == GrabSuccess)
        {
            pointer_grabbed = true;
            return true;
        }

        // Not fatal: just don’t confine.
        if (diagnostics)
        {
            STRATA_LOG_WARN(diagnostics->logger(),
                            "platform",
                            "X11: XGrabPointer failed (code={})",
                            r);
        }
        pointer_grabbed = false;
        return false;
    }

    void warp_pointer_to_center() noexcept
    {
        if (!display || !window)
            return;

        if (cached_w <= 0 || cached_h <= 0)
            return;

        int const cx = cached_w / 2;
        int const cy = cached_h / 2;

        ::XWarpPointer(display, None, window, 0, 0, 0, 0, cx, cy);
        ::XFlush(display);

        last_mouse_x       = cx;
        last_mouse_y       = cy;
        mouse_pos_valid    = true;
        ignore_next_motion = true;
    }

    void apply_cursor_mode() noexcept
    {
        if (!display || !window)
            return;

        // Never keep pointer grabbed when unfocused or minimized.
        if (!input.focused() || minimized)
        {
            ungrab_pointer();
            ::XUndefineCursor(display, window);
            ::XFlush(display);
            mouse_pos_valid    = false;
            ignore_next_motion = false;
            return;
        }

        switch (cursor_mode)
        {
        case CursorMode::Normal:
            ungrab_pointer();
            ::XUndefineCursor(display, window);
            break;

        case CursorMode::Hidden:
            ungrab_pointer();
            ensure_invisible_cursor();
            if (invisible_cursor_ready)
            {
                ::XDefineCursor(display, window, invisible_cursor);
            }
            break;

        case CursorMode::Confined:
            // visible cursor + grab pointer confined to window
            ensure_invisible_cursor(); // (not required, but cheap)
            ::XUndefineCursor(display, window);
            (void)grab_pointer(true, None);
            break;

        case CursorMode::Locked:
            ensure_invisible_cursor();
            // hidden cursor + grab pointer confined to window
            (void)grab_pointer(true, invisible_cursor_ready ? invisible_cursor : None);
            // FPS-style endless deltas: warp to center
            warp_pointer_to_center();
            break;
        }

        ::XFlush(display);
    }

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

        cached_w = desc.size.width;
        cached_h = desc.size.height;

        wm_delete = ::XInternAtom(display, "WM_DELETE_WINDOW", False);
        if (wm_delete)
        {
            ::XSetWMProtocols(display, window, &wm_delete, 1);
        }

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
        if (display)
        {
            ungrab_pointer();
            if (window)
            {
                ::XUndefineCursor(display, window);
            }
            if (invisible_cursor_ready)
            {
                ::XFreeCursor(display, invisible_cursor);
                invisible_cursor       = 0;
                invisible_cursor_ready = false;
            }
        }

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

        input.begin_frame();

        while (::XPending(display) > 0)
        {
            XEvent evt{};
            ::XNextEvent(display, &evt);

            switch (evt.type)
            {
            case FocusIn:
                input.set_focused(true);
                mouse_pos_valid    = false;
                ignore_next_motion = false;
                apply_cursor_mode();
                break;

            case FocusOut:
                input.set_focused(false);
                mouse_pos_valid    = false;
                ignore_next_motion = false;
                apply_cursor_mode();
                break;

            case ConfigureNotify:
                cached_w = evt.xconfigure.width;
                cached_h = evt.xconfigure.height;
                // If locked, keep center-warp stable after size change.
                if (cursor_mode == CursorMode::Locked && input.focused())
                {
                    warp_pointer_to_center();
                }
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
                if (XEventsQueued(display, QueuedAfterReading) > 0)
                {
                    XEvent next{};
                    XPeekEvent(display, &next);

                    if (next.type == KeyPress &&
                        next.xkey.keycode == evt.xkey.keycode &&
                        next.xkey.time == evt.xkey.time)
                    {
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

                if (ignore_next_motion)
                {
                    ignore_next_motion = false;
                    last_mouse_x       = x;
                    last_mouse_y       = y;
                    mouse_pos_valid    = true;
                    break;
                }

                if (mouse_pos_valid)
                {
                    int const dx = x - last_mouse_x;
                    int const dy = y - last_mouse_y;
                    input.add_mouse_delta(static_cast<float>(dx), static_cast<float>(dy));
                }

                last_mouse_x    = x;
                last_mouse_y    = y;
                mouse_pos_valid = true;

                if (cursor_mode == CursorMode::Locked)
                {
                    warp_pointer_to_center();
                }

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
                apply_cursor_mode();
                break;

            case MapNotify:
                visible   = true;
                minimized = false;
                apply_cursor_mode();
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

// -----------------------------------------------------------------------------
// Window API
// -----------------------------------------------------------------------------

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

void Window::set_cursor_mode(CursorMode mode) noexcept
{
    if (!p_ || !p_->display || !p_->window)
        return;

    if (p_->cursor_mode == mode)
        return;

    p_->cursor_mode        = mode;
    p_->mouse_pos_valid    = false;
    p_->ignore_next_motion = false;

    p_->apply_cursor_mode();
}

CursorMode Window::cursor_mode() const noexcept
{
    return p_ ? p_->cursor_mode : CursorMode::Normal;
}

bool Window::has_focus() const noexcept
{
    return p_ ? p_->input.focused() : false;
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
