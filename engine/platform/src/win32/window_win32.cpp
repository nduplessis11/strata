// -----------------------------------------------------------------------------
// engine/platform/src/win32/window_win32.cpp
//
// Purpose:
//   Win32 backend implementation for strata::platform::Window. Creates and
//   manages native windows, message dispatch, and WSI handles for the graphics
//   layer.
// -----------------------------------------------------------------------------

#include <windows.h>
#include <string>
#include <utility>

#include "strata/platform/window.h"
#include "strata/platform/wsi_handle.h"

namespace {

    // ────────────────────────────────────────────────────────────────────────────────
    // A unique class name for this process.
    // Each "window class" in Win32 describes default behavior (cursor, icon, WndProc).
    // We register it once, then use it to create one or more windows of that class.
    constexpr const wchar_t* kStrataWndClass = L"strata_window_class";

    // Register the window class once per process.
    // NOTES on key fields:
    //  - CS_OWNDC: give each window its own device context (useful for GDI, harmless otherwise).
    //  - CS_HREDRAW | CS_VREDRAW: request repaint on horizontal/vertical size changes.
    //  - lpfnWndProc: the function Windows calls for EVERY message (clicks, sizing, focus, etc.).
    //  - hbrBackground = nullptr: don't auto-erase background → reduces flicker in renderers.
    //  - If the class is already registered (typical in multi-window engines),
    //    treat that as success; registration is idempotent for our purposes.
    ATOM register_wnd_class(HINSTANCE hinst, WNDPROC proc) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = proc;          // our static thunk (see Impl below)
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hinst;
        wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;       // no background erase → less flicker
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = kStrataWndClass;
        wc.hIconSm = wc.hIcon;

        ATOM atom = ::RegisterClassExW(&wc);
        if (!atom && ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
            // Already registered by a previous window → fine; treat as success.
            atom = static_cast<ATOM>(1);
        }
        return atom;
    }

    // UTF-8 → UTF-16 for window titles.
    // Public API uses UTF-8 (std::string_view); Win32 "W" APIs use UTF-16 (wchar_t*).
    std::wstring utf8_to_wide(std::string_view s) {
        if (s.empty()) return std::wstring();
        const int needed = ::MultiByteToWideChar(
            CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);

        std::wstring w;
        if (needed > 0) {
            w.resize(needed);
            ::MultiByteToWideChar(
                CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), needed);
        }
        return w;
    }

} // namespace

namespace strata::platform {

    // ────────────────────────────────────────────────────────────────────────────────
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
    struct Window::Impl {
        HINSTANCE hinstance{};
        HWND      hwnd{};
        bool      closing{ false };
        bool      minimized{ false };
        HBRUSH    clear_brush{};    // TEMP: dark-gray fill for smoke test (renderer-ready)

        // Instance WndProc: receives messages after GWLP_USERDATA holds our Impl*.
        LRESULT wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
            switch (msg) {
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
                return 0;

            case WM_ERASEBKGND:
                // Prevent the OS from erasing the background separately (reduces flicker).
                // We fully cover the client area in WM_PAINT (or with the renderer later).
                return 1;

            case WM_PAINT: {
                // TEMPORARY SMOKE TEST: Fill the invalid region so grows aren't black.
                // Later, when Vulkan is wired, this block becomes BeginPaint/EndPaint only.
                PAINTSTRUCT ps{};
                HDC dc{ ::BeginPaint(h, &ps) };

                // Lazy-create a neutral dark gray brush
                if (!clear_brush) {
                    clear_brush = ::CreateSolidBrush(RGB(32, 32, 32));
                }

                ::FillRect(dc, &ps.rcPaint, clear_brush);
                ::EndPaint(h, &ps);
                return 0;
            }
            case WM_NCDESTROY:
                // Final teardown: break association, release GDI resources, mark closed.
                ::SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                if (clear_brush) {
                    ::DeleteObject(clear_brush);
                    clear_brush = nullptr;
                }
                hwnd = nullptr;
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
        static LRESULT CALLBACK wndproc_static(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
            if (msg == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
                if (cs && cs->lpCreateParams) {
                    ::SetWindowLongPtrW(
                        hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                }
            }
            auto* impl = reinterpret_cast<Impl*>(
                ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (impl) {
                return impl->wnd_proc(hwnd, msg, w, l);
            }
            return ::DefWindowProcW(hwnd, msg, w, l);
        }

        // Build a strongly-typed WSI descriptor for the gfx layer.
        // No OS headers leak outside; gfx later casts back to HWND/HINSTANCE to call
        // vkCreateWin32SurfaceKHR in a single "WSI bridge" TU.
        WsiHandle make_wsi_handle() const {
            using namespace wsi;
            Win32 h{};
            h.instance.value = reinterpret_cast<std::uintptr_t>(hinstance);
            h.window.value = reinterpret_cast<std::uintptr_t>(hwnd);
            return WsiHandle{ std::in_place_type<Win32>, h };
        }
    };

    // ────────────────────────────────────────────────────────────────────────────────
    // Window API — construct, pump, query, teardown

    Window::Window(const WindowDesc& desc)
        : p_(std::make_unique<Impl>()) {

        // Module handle of this EXE/DLL.
        p_->hinstance = ::GetModuleHandleW(nullptr);

        // Register our window class (idempotent).
        if (!register_wnd_class(p_->hinstance, &Impl::wndproc_static)) {
            p_->closing = true;
            return;
        }

        // Choose style flags. If not resizable, remove thick frame + maximize box.
        DWORD style = WS_OVERLAPPEDWINDOW;
        DWORD ex = WS_EX_APPWINDOW;
        if (!desc.resizable) {
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }

        // Convert desired client size → outer window rect for this style.
        RECT rect{ 0, 0, desc.size.width, desc.size.height };
        ::AdjustWindowRectEx(&rect, style, FALSE, ex);
        const int outer_w = rect.right - rect.left;
        const int outer_h = rect.bottom - rect.top;

        // Title: UTF-8 → UTF-16.
        std::wstring wtitle = utf8_to_wide(desc.title);

        // Create the window. Pass Impl* via lpCreateParams so WM_NCCREATE can stash it.
        HWND hwnd = ::CreateWindowExW(
            ex, kStrataWndClass, wtitle.c_str(), style,
            CW_USEDEFAULT, CW_USEDEFAULT, outer_w, outer_h,
            nullptr, nullptr, p_->hinstance, p_.get());

        if (!hwnd) {
            p_->closing = true;
            return;
        }

        p_->hwnd = hwnd;

        // Initial visibility.
        if (desc.visible) {
            ::ShowWindow(p_->hwnd, SW_SHOW);
            ::UpdateWindow(p_->hwnd); // trigger an immediate paint if needed
        }
        else {
            ::ShowWindow(p_->hwnd, SW_HIDE);
        }
    }

    Window::~Window() = default;

    // Move-only owner of the native window (unique_ptr pImpl makes copy deleted).
    Window::Window(Window&&) noexcept = default;
    Window& Window::operator=(Window&&) noexcept = default;

    // Has the user (or OS) requested close? (Set on WM_CLOSE in wnd_proc.)
    bool Window::should_close() const noexcept {
        return p_->closing;
    }

    // Asynchronously request a close by posting WM_CLOSE to this window.
    void Window::request_close() noexcept {
        if (p_->hwnd) {
            ::PostMessageW(p_->hwnd, WM_CLOSE, 0, 0);
        }
    }

    // Non-blocking message pump for game loops.
    // Processes ALL messages for this GUI thread (nullptr filter).
    void Window::poll_events() {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    // Change the window title (public API uses UTF-8).
    void Window::set_title(std::string_view title) {
        if (!p_->hwnd) return;
        const std::wstring w = utf8_to_wide(title);
        ::SetWindowTextW(p_->hwnd, w.c_str());
    }

    // Client area size (logical units). The render area equals client for now.
    // If you enable Per-Monitor DPI Awareness (PMv2), use GetDpiForWindow()
    // to compute true pixel framebuffer size in framebuffer_size().
    auto Window::window_size() const noexcept -> std::pair<std::int32_t, std::int32_t> {
        if (!p_->hwnd) return { 0, 0 };
        RECT rc{};
        ::GetClientRect(p_->hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        return { w, h };
    }

    auto Window::framebuffer_size() const noexcept -> std::pair<std::int32_t, std::int32_t> {
        // FIRST BRING-UP: assume client == framebuffer.
        // LATER: if DPI-aware, multiply by DPI/96 or use GetDpiForWindow().
        return window_size();
    }

    // Return a typed WSI descriptor for gfx (used to create a VkSurfaceKHR).
    WsiHandle Window::native_wsi() const noexcept {
        return p_->make_wsi_handle();
    }

} // namespace strata::platform
