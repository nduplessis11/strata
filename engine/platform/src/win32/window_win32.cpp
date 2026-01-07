// -----------------------------------------------------------------------------
// engine/platform/src/win32/window_win32.cpp
//
// Purpose:
//   Win32 backend implementation for strata::platform::Window. Creates and
//   manages native windows, message dispatch, and WSI handles for the graphics
//   layer.
//
// V1 Camera Input:
//   - Track raw input state (keys, mouse buttons, mouse delta) per Window.
//   - Reset per-frame deltas in poll_events().
//
// Cursor control (CursorMode):
//   - Normal:   visible, not confined
//   - Hidden:   hidden, not confined
//   - Confined: visible, confined to client rect while focused
//   - Locked:   hidden + confined; additionally warp-to-center for endless deltas
// -----------------------------------------------------------------------------

#include <cstddef> // std::byte
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <windowsx.h> // for GET_X_LPARAM, GET_Y_LPARAM

#include "strata/base/diagnostics.h"
#include "strata/platform/input.h"
#include "strata/platform/window.h"
#include "strata/platform/wsi_handle.h"

namespace
{

// -----------------------------------------------------------------------------
// A unique class name for this process.
// Each "window class" in Win32 describes default behavior (cursor, icon, WndProc).
// We register it once, then use it to create one or more windows of that class.
constexpr wchar_t const* strata_wnd_class = L"strata_window_class";

// Register the window class once per process.
// NOTES on key fields:
//  - CS_OWNDC: give each window its own device context (useful for GDI, harmless otherwise).
//  - CS_HREDRAW | CS_VREDRAW: request repaint on horizontal/vertical size changes.
//  - lpfnWndProc: the function Windows calls for EVERY message (clicks, sizing, focus, etc.).
//  - hbrBackground = nullptr: don't auto-erase background -> reduces flicker in renderers.
//  - If the class is already registered (typical in multi-window engines),
//    treat that as success; registration is idempotent for our purposes.
ATOM register_wnd_class(HINSTANCE hinst, WNDPROC proc)
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = proc; // our static thunk (see Impl below)
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hinst;
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // no background erase → less flicker
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = strata_wnd_class;
    wc.hIconSm       = wc.hIcon;

    ATOM atom = ::RegisterClassExW(&wc);
    if (!atom && ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        // Already registered by a previous window -> fine; treat as success.
        atom = static_cast<ATOM>(1);
    }
    return atom;
}

// UTF-8 -> UTF-16 for window titles.
// Public API uses UTF-8 (std::string_view); Win32 "W" APIs use UTF-16 (wchar_t*).
std::wstring utf8_to_wide(std::string_view s)
{
    if (s.empty())
        return std::wstring();
    int const needed =
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);

    std::wstring w;
    if (needed > 0)
    {
        w.resize(needed);
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), needed);
    }
    return w;
}

} // namespace

namespace strata::platform
{

// -----------------------------------------------------------------------------
// Window::Impl — private Win32 state (pImpl)
//
// WHY pImpl?
//  - Keeps <windows.h> out of public headers.
//  - Lets us change backend details without breaking public ABI.
//  - Works with /permissive-: private nested types can be named inside members.
//
// WHY the "static thunk"?
//  - Win32 requires a free/static WNDPROC function.
//  - Free functions can't legally name private nested types under /permissive-.
//  - We put the WNDPROC THUNK *inside* Impl so it can name Impl,
//    then stash Impl* in GWLP_USERDATA on WM_NCCREATE and forward
//    all later messages to the instance handler (wnd_proc).
struct Window::Impl
{
    base::Diagnostics* diagnostics{nullptr};

    HINSTANCE hinstance{};
    HWND      hwnd{};
    bool      closing{false};
    bool      minimized{false};
    HBRUSH    clear_brush{}; // TEMP: dark-gray fill for smoke test (renderer-ready)

    // V1 Camera Input: input state owned by this window.
    InputState input{};

    bool mouse_pos_valid{false};
    int  last_mouse_x{0};
    int  last_mouse_y{0};

    // Cursor Control
    CursorMode cursor_mode{CursorMode::Normal};
    bool       ignore_next_mouse_move{false};

    // Raw input (relative mouse deltas). Use this for Locked mode to avoid
    // jitter from SetCursorPos() warp + WM_MOUSEMOVE.
    bool                   raw_mouse_enabled{false};
    std::vector<std::byte> raw_input_buffer{};

    // --- Cursor helpers ------------------------------------------------------

    [[nodiscard]]
    RECT client_rect_screen() const noexcept
    {
        RECT rc{};
        ::GetClientRect(hwnd, &rc);

        POINT tl{rc.left, rc.top};
        POINT br{rc.right, rc.bottom};

        ::ClientToScreen(hwnd, &tl);
        ::ClientToScreen(hwnd, &br);

        RECT out{};
        out.left   = tl.x;
        out.top    = tl.y;
        out.right  = br.x;
        out.bottom = br.y;
        return out;
    }

    void apply_clip(bool enable) const noexcept
    {
        if (!hwnd)
            return;

        if (!enable)
        {
            ::ClipCursor(nullptr);
            return;
        }

        RECT const clip = client_rect_screen();
        ::ClipCursor(&clip);
    }

    [[nodiscard]]
    std::pair<std::int32_t, std::int32_t> client_center() const noexcept
    {
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        std::int32_t const cx = (rc.right - rc.left) / 2;
        std::int32_t const cy = (rc.bottom - rc.top) / 2;
        return {cx, cy};
    }

    void set_cursor_visible(bool visible) noexcept
    {
        // Prefer SM_SETCURSOR + SetCursor to avoid ShowCursor refcount pitfalls.
        if (visible)
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
        }
        else
        {
            ::SetCursor(nullptr);
        }
    }

    void center_cursor_screen() noexcept
    {
        if (!hwnd)
            return;

        auto const [cx, cy] = client_center();

        POINT p{cx, cy};
        ::ClientToScreen(hwnd, &p);
        ::SetCursorPos(p.x, p.y);

        // Make delta math robust immediately.
        last_mouse_x           = cx;
        last_mouse_y           = cy;
        mouse_pos_valid        = true;
        ignore_next_mouse_move = true;
        input.set_mouse_pos(cx, cy);
    }

    void try_enable_raw_mouse() noexcept
    {
        if (!hwnd)
            return;

        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01; // Generic Desktop Controls
        rid.usUsage     = 0x02; // Mouse
        rid.dwFlags     = 0;    // keep legacy WM_MOUSEMOVE for non-locked modes
        rid.hwndTarget  = hwnd;

        if (::RegisterRawInputDevices(&rid, 1, sizeof(rid)) == FALSE)
        {
            raw_mouse_enabled = false;
            if (diagnostics)
            {
                STRATA_LOG_WARN(
                    diagnostics->logger(),
                    "platform.win32",
                    "RegisterRawInputDevices(mouse) failed; falling back to WM_MOUSEMOVE deltas");
            }
            return;
        }

        raw_mouse_enabled = true;
    }

    void on_raw_input(LPARAM lparam) noexcept
    {
        if (!raw_mouse_enabled)
            return;
        if (!input.focused())
            return;
        if (cursor_mode != CursorMode::Locked)
            return;

        UINT size = 0;
        if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),
                              RID_INPUT,
                              nullptr,
                              &size,
                              sizeof(RAWINPUTHEADER)) != 0)
        {
            return;
        }
        if (size == 0)
            return;

        if (raw_input_buffer.size() < size)
            raw_input_buffer.resize(size);

        UINT const read = ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),
                                            RID_INPUT,
                                            raw_input_buffer.data(),
                                            &size,
                                            sizeof(RAWINPUTHEADER));

        if (read != size)
            return;

        RAWINPUT const* raw = reinterpret_cast<RAWINPUT const*>(raw_input_buffer.data());
        if (raw->header.dwType != RIM_TYPEMOUSE)
            return;

        RAWMOUSE const& m = raw->data.mouse;

        // Most mice deliver relative motion. If a device reports absolute, ignore for now.
        if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
            return;

        if (m.lLastX != 0 || m.lLastY != 0)
        {
            input.add_mouse_delta(static_cast<float>(m.lLastX), static_cast<float>(m.lLastY));
        }
    }

    void apply_cursor_mode() noexcept
    {
        if (!hwnd)
            return;

        // Never keep the cursor clipped when unfocused or minimized.
        bool const focused = input.focused();
        if (!focused || minimized)
        {
            apply_clip(false);
            set_cursor_visible(true);
            mouse_pos_valid        = false;
            ignore_next_mouse_move = false;
            return;
        }

        switch (cursor_mode)
        {
        case CursorMode::Normal:
            apply_clip(false);
            set_cursor_visible(true);
            break;

        case CursorMode::Hidden:
            apply_clip(false);
            set_cursor_visible(false);
            break;

        case CursorMode::Confined:
            apply_clip(true);
            set_cursor_visible(true);
            break;

        case CursorMode::Locked:
            apply_clip(true);
            set_cursor_visible(false);
            // In Locked mode we prefer WM_INPUT (raw deltas) and DO NOT warp per-mousemove.
            // Center once on entry to keep the cursor away from edges (nice when unlocking).
            center_cursor_screen();
            break;
        }
    }

    // --- Input mapping -------------------------------------------------------

    void on_key(WPARAM vk, bool down) noexcept
    {
        switch (vk)
        {
        case 'W':
            input.set_key(Key::W, down);
            break;
        case 'A':
            input.set_key(Key::A, down);
            break;
        case 'S':
            input.set_key(Key::S, down);
            break;
        case 'D':
            input.set_key(Key::D, down);
            break;

        case VK_SPACE:
            input.set_key(Key::Space, down);
            break;

        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            input.set_key(Key::Ctrl, down);
            break;

        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            input.set_key(Key::Shift, down);
            break;

        case VK_ESCAPE:
            input.set_key(Key::Escape, down);
            break;

        default:
            break;
        }
    }

    void on_mouse_button(UINT msg, bool down) noexcept
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            input.set_mouse_button(MouseButton::Left, down);
            break;

        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            input.set_mouse_button(MouseButton::Right, down);
            break;

        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            input.set_mouse_button(MouseButton::Middle, down);
            break;

        default:
            break;
        }
    }

    // Instance WndProc: receives messages after GWLP_USERDATA holds our Impl*.
    LRESULT wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        switch (msg)
        {
        case WM_INPUT:
            on_raw_input(l);
            // IMPORTANT: Win32 docs require calling DefWindowProc for WM_INPUT (cleanup).
            return ::DefWindowProcW(h, msg, w, l);

        case WM_SETFOCUS:
            input.set_focused(true);
            mouse_pos_valid        = false;
            ignore_next_mouse_move = false;
            apply_cursor_mode();
            return 0;

        case WM_KILLFOCUS:
            input.set_focused(false);
            mouse_pos_valid        = false;
            ignore_next_mouse_move = false;
            apply_cursor_mode();
            return 0;

        case WM_SETCURSOR:
        {
            // Hide cursor in client area for Hidden/Locked.
            // (This is more reliable than ShowCursor refcount games.)
            if (LOWORD(l) == HTCLIENT &&
                input.focused() &&
                (cursor_mode == CursorMode::Hidden || cursor_mode == CursorMode::Locked))
            {
                ::SetCursor(nullptr);
                return TRUE;
            }
            break;
        }

        case WM_MOVE:
            apply_cursor_mode();
            return 0;

        case WM_KEYDOWN:
            on_key(w, true);
            return 0;

        case WM_KEYUP:
            on_key(w, false);
            return 0;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        {
            // Always track it in input state (so Alt etc. is visible to the engine).
            on_key(w, msg == WM_SYSKEYDOWN);

            // IMPORTANT:
            // Don't swallow system keys by default - let Windows generate SC_CLOSE for Alt+F4 etc.
            //
            // Exception: many engines suppress "press Alt" / F10 from activating the system menu
            // focus.
            if (w == VK_MENU || w == VK_F10)
                return 0;

            return ::DefWindowProcW(h, msg, w, l);
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            on_mouse_button(msg, true);
            break;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            on_mouse_button(msg, false);
            return 0;

        case WM_MOUSEMOVE:
        {
            if (!input.focused())
                return 0;

            // In Locked mode, use raw input deltas. Ignoring WM_MOUSEMOVE avoids:
            // - synthetic warp moves
            // - DPI rounding noise
            // - double-counting motion
            if (cursor_mode == CursorMode::Locked && raw_mouse_enabled)
                return 0;

            std::int32_t const x = GET_X_LPARAM(l);
            std::int32_t const y = GET_Y_LPARAM(l);
            input.set_mouse_pos(x, y);

            // Ignore the synthetic mouse move generated by warping-to-center.
            if (ignore_next_mouse_move)
            {
                ignore_next_mouse_move = false;
                last_mouse_x           = x;
                last_mouse_y           = y;
                mouse_pos_valid        = true;
                return 0;
            }

            if (mouse_pos_valid)
            {
                std::int32_t const dx = x - last_mouse_x;
                std::int32_t const dy = y - last_mouse_y;
                input.add_mouse_delta(static_cast<float>(dx), static_cast<float>(dy));
            }

            last_mouse_x    = x;
            last_mouse_y    = y;
            mouse_pos_valid = true;

            // Legacy fallback: if raw input isn't available, keep old warp behavior.
            if (cursor_mode == CursorMode::Locked && !raw_mouse_enabled)
            {
                center_cursor_screen();
            }

            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            // Positive is wheel away from user. Normalize to "notches".
            std::int16_t const delta = GET_WHEEL_DELTA_WPARAM(w);
            input.add_wheel_delta(static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA));
            return 0;
        }

        case WM_CLOSE:
            // User requested close (e.g., Alt-F4 or clicking "X").
            // We don't destroy here; we mark and let Application drive teardown.
            closing = true;
            return 0;

        case WM_DESTROY:
            // For single-window apps, post a quit message so the thread's message
            // loop can exit if anyone is waiting on it.
            ::PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            // Track minimized state; the render loop can throttle when minimized.
            minimized = (w == SIZE_MINIMIZED);
            // Ask Windows to send WM_PAINT soon; don't erase (we'll paint everything).
            ::InvalidateRect(h, nullptr, FALSE);
            apply_cursor_mode(); // update clip region or release if minimized
            return 0;

        case WM_ERASEBKGND:
            // Prevent the OS from erasing the background separately (reduces flicker).
            // We fully cover the client area in WM_PAINT (or with the renderer later).
            return 1;

        case WM_PAINT:
        {
            // TEMPORARY SMOKE TEST: Fill the invalid region so grows aren't black.
            // Later, when Vulkan is wired, this block becomes BeginPaint/EndPaint only.
            PAINTSTRUCT ps{};
            HDC         dc{::BeginPaint(h, &ps)};

            // Lazy-create a neutral dark gray brush
            if (!clear_brush)
            {
                clear_brush = ::CreateSolidBrush(RGB(32, 32, 32));
            }

            ::FillRect(dc, &ps.rcPaint, clear_brush);
            ::EndPaint(h, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            // Final teardown: break association, release GDI resources, mark closed.

            // Ensure we never leave the user cursor-clipped.
            ::ClipCursor(nullptr);
            ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));

            ::SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            if (clear_brush)
            {
                ::DeleteObject(clear_brush);
                clear_brush = nullptr;
            }
            hwnd    = nullptr;
            closing = true;
            return ::DefWindowProcW(h, msg, w, l); // returning 0 is also fine

        default:
            break;
        }
        return ::DefWindowProcW(h, msg, w, l);
    }

    // Static THUNK: allowed to name Impl because it's a member.
    // 1) On WM_NCCREATE, we receive lpCreateParams (our Impl*) and store it in
    //    per-window storage (GWLP_USERDATA).
    // 2) Afterwards, fetch Impl* and forward messages to the instance handler.
    static LRESULT CALLBACK wndproc_static(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
    {
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            if (cs && cs->lpCreateParams)
            {
                ::SetWindowLongPtrW(hwnd,
                                    GWLP_USERDATA,
                                    reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            }
        }
        auto* impl = reinterpret_cast<Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (impl)
        {
            return impl->wnd_proc(hwnd, msg, w, l);
        }
        return ::DefWindowProcW(hwnd, msg, w, l);
    }

    // Build a strongly-typed WSI descriptor for the gfx layer.
    // No OS headers leak outside; gfx later casts back to HWND/HINSTANCE to call
    // vkCreateWin32SurfaceKHR in a single "WSI bridge" TU.
    WsiHandle make_wsi_handle() const
    {
        using namespace wsi;
        Win32 h{};
        h.instance.value = reinterpret_cast<std::uintptr_t>(hinstance);
        h.window.value   = reinterpret_cast<std::uintptr_t>(hwnd);
        return WsiHandle{std::in_place_type<Win32>, h};
    }
};

// -----------------------------------------------------------------------------
// Window API — construct, pump, query, teardown
// -----------------------------------------------------------------------------

Window::Window(base::Diagnostics& diagnostics, WindowDesc const& desc)
      : p_(std::make_unique<Impl>())
{
    p_->diagnostics = &diagnostics;

    // Module handle of this EXE/DLL.
    p_->hinstance = ::GetModuleHandleW(nullptr);

    // Register our window class (idempotent).
    if (!register_wnd_class(p_->hinstance, &Impl::wndproc_static))
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "platform", "Win32: RegisterClassExW failed");
        p_->closing = true;
        return;
    }

    // Choose style flags. If not resizable, remove thick frame + maximize box.
    DWORD       style = WS_OVERLAPPEDWINDOW;
    DWORD const ex    = WS_EX_APPWINDOW;
    if (!desc.resizable)
    {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    // Convert desired client size → outer window rect for this style.
    RECT rect{0, 0, desc.size.width, desc.size.height};
    ::AdjustWindowRectEx(&rect, style, FALSE, ex);
    int const outer_w = rect.right - rect.left;
    int const outer_h = rect.bottom - rect.top;

    // Title: UTF-8 -> UTF-16.
    std::wstring const wtitle = utf8_to_wide(desc.title);

    // Create the window. Pass Impl* via lpCreateParams so WM_NCCREATE can stash it.
    HWND hwnd = ::CreateWindowExW(ex,
                                  strata_wnd_class,
                                  wtitle.c_str(),
                                  style,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  outer_w,
                                  outer_h,
                                  nullptr,
                                  nullptr,
                                  p_->hinstance,
                                  p_.get());

    if (!hwnd)
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "platform", "Win32: CreateWindowExW failed");
        p_->closing = true;
        return;
    }

    p_->hwnd = hwnd;

    // Enable raw mouse input (used in Locked mode to avoid jitter).
    p_->try_enable_raw_mouse();

    // Initial visibility.
    if (desc.visible)
    {
        ::ShowWindow(p_->hwnd, SW_SHOW);
        ::UpdateWindow(p_->hwnd); // trigger an immediate paint if needed
    }
    else
    {
        ::ShowWindow(p_->hwnd, SW_HIDE);
    }

    // Apply any non-default cursor mode after creation.
    p_->apply_cursor_mode();
}

Window::~Window() = default;

// Move-only owner of the native window (unique_ptr pImpl makes copy deleted).
Window::Window(Window&&) noexcept            = default;
Window& Window::operator=(Window&&) noexcept = default;

// Has the user (or OS) requested close? (Set on WM_CLOSE in wnd_proc.)
bool Window::should_close() const noexcept
{
    return p_->closing;
}

// Asynchronously request a close by posting WM_CLOSE to this window.
void Window::request_close() noexcept
{
    if (p_->hwnd)
    {
        ::PostMessageW(p_->hwnd, WM_CLOSE, 0, 0);
    }
}

// Non-blocking message pump for game loops.
// Processes ALL messages for this GUI thread (nullptr filter).
void Window::poll_events()
{
    // V1 Camera Input: reset per-frame deltas before pumping.
    if (p_)
        p_->input.begin_frame();

    MSG msg;
    // Temp: per-window message pump.
    // TODO: Move to Application or platform::EventLoop
    while (::PeekMessageW(&msg, p_->hwnd, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

// Change the window title (public API uses UTF-8).
void Window::set_title(std::string_view title)
{
    if (!p_->hwnd)
        return;
    std::wstring const w = utf8_to_wide(title);
    ::SetWindowTextW(p_->hwnd, w.c_str());
}

void Window::set_cursor_mode(CursorMode mode) noexcept
{
    if (!p_ || !p_->hwnd)
        return;

    if (p_->cursor_mode == mode)
        return;

    p_->cursor_mode            = mode;
    p_->mouse_pos_valid        = false;
    p_->ignore_next_mouse_move = false;

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

// Client area size (logical units). The render area equals client for now.
// If you enable Per-Monitor DPI Awareness (PMv2), use GetDpiForWindow()
// to compute true pixel framebuffer size in framebuffer_size().
auto Window::window_size() const noexcept -> std::pair<std::int32_t, std::int32_t>
{
    if (!p_->hwnd)
        return {0, 0};
    RECT rc{};
    ::GetClientRect(p_->hwnd, &rc);
    std::int32_t const w = rc.right - rc.left;
    std::int32_t const h = rc.bottom - rc.top;
    return {w, h};
}

auto Window::framebuffer_size() const noexcept -> std::pair<std::int32_t, std::int32_t>
{
    // FIRST BRING-UP: assume client == framebuffer.
    // LATER: if DPI-aware, multiply by DPI/96 or use GetDpiForWindow().
    return window_size();
}

bool Window::is_minimized() const noexcept
{
    return p_ ? p_->minimized : true;
}

bool Window::is_visible() const noexcept
{
    return (p_ && p_->hwnd) ? (::IsWindowVisible(p_->hwnd) != 0) : false;
}

auto Window::input() const noexcept -> InputState const&
{
    return p_->input;
}

// Return a typed WSI descriptor for gfx (used to create a VkSurfaceKHR).
WsiHandle Window::native_wsi() const noexcept
{
    return p_->make_wsi_handle();
}

} // namespace strata::platform
