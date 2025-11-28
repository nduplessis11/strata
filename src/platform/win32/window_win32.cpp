#include "platform/window.h"

#if defined(_WIN32)

#include <windows.h>

#include <algorithm>
#include <string>

#include "platform/wsi_handle.h"

namespace {
    constexpr const wchar_t* kStrataWndClass = L"strata_window_class";

    ATOM register_wnd_class(HINSTANCE hinst, WNDPROC proc) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = proc;
        wc.hInstance = hinst;
        wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kStrataWndClass;
        wc.hIconSm = wc.hIcon;

        ATOM atom = ::RegisterClassExW(&wc);
        if (!atom && ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
            atom = static_cast<ATOM>(1);
        }
        return atom;
    }

    std::wstring utf8_to_wide(std::string_view s) {
        if (s.empty()) return std::wstring();
        const int needed = ::MultiByteToWideChar(
            CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);

        std::wstring w;
        if (needed > 0) {
            w.resize(needed);
            ::MultiByteToWideChar(
                CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                w.data(), needed);
        }
        return w;
    }
} // namespace

namespace strata::platform {

    struct Window::Impl {
        HINSTANCE  hinstance{};
        HWND       hwnd{};
        bool       closing{ false };
        bool       minimized{ false };
        bool       visible{ false };
        HBRUSH     clear_brush{};
        WindowDesc desc{};

        LRESULT wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
            switch (msg) {
            case WM_CLOSE:
                closing = true;
                return 0;

            case WM_DESTROY:
                ::PostQuitMessage(0);
                return 0;

            case WM_SIZE:
                minimized = (w == SIZE_MINIMIZED);
                ::InvalidateRect(h, nullptr, FALSE);
                return 0;

            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc{ ::BeginPaint(h, &ps) };
                if (!clear_brush) {
                    clear_brush = ::CreateSolidBrush(RGB(16, 16, 16));
                }

                RECT client{};
                ::GetClientRect(h, &client);
                ::FillRect(dc, &client, clear_brush);

                POINT tri[3];
                const int w = client.right - client.left;
                const int h_px = client.bottom - client.top;
                const int cx = w / 2;
                const int cy = h_px / 2;
                const int half = static_cast<int>(0.35f * std::min(w, h_px));

                tri[0] = POINT{ cx, cy - half };
                tri[1] = POINT{ cx - half, cy + half };
                tri[2] = POINT{ cx + half, cy + half };

                HBRUSH tri_brush = ::CreateSolidBrush(RGB(229, 115, 57));
                HPEN pen = ::CreatePen(PS_SOLID, 2, RGB(255, 196, 143));
                HGDIOBJ old_brush = ::SelectObject(dc, tri_brush);
                HGDIOBJ old_pen = ::SelectObject(dc, pen);
                ::Polygon(dc, tri, 3);
                ::SelectObject(dc, old_brush);
                ::SelectObject(dc, old_pen);
                ::DeleteObject(tri_brush);
                ::DeleteObject(pen);
                ::EndPaint(h, &ps);
                return 0;
            }

            case WM_NCDESTROY:
                ::SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                if (clear_brush) {
                    ::DeleteObject(clear_brush);
                    clear_brush = nullptr;
                }
                hwnd = nullptr;
                closing = true;
                return ::DefWindowProcW(h, msg, w, l);

            default:
                break;
            }
            return ::DefWindowProcW(h, msg, w, l);
        }

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

        WsiHandle make_wsi_handle() const {
            using namespace wsi;
            Win32 h{};
            h.instance.value = reinterpret_cast<std::uintptr_t>(hinstance);
            h.window.value = reinterpret_cast<std::uintptr_t>(hwnd);
            return WsiHandle{ std::in_place_type<Win32>, h };
        }
    };

    namespace detail {
        Window::Impl* create_window_impl(const WindowDesc& desc) {
            auto* impl = new Window::Impl{};
            impl->desc = desc;
            impl->hinstance = ::GetModuleHandleW(nullptr);

            if (!register_wnd_class(impl->hinstance, &Window::Impl::wndproc_static)) {
                impl->closing = true;
                return impl;
            }

            DWORD style = WS_OVERLAPPEDWINDOW;
            DWORD ex = WS_EX_APPWINDOW;
            if (!desc.resizable) {
                style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
            }

            RECT rect{ 0, 0, desc.size.width, desc.size.height };
            ::AdjustWindowRectEx(&rect, style, FALSE, ex);
            const int outer_w = rect.right - rect.left;
            const int outer_h = rect.bottom - rect.top;

            std::wstring wtitle = utf8_to_wide(desc.title);

            impl->hwnd = ::CreateWindowExW(
                ex, kStrataWndClass, wtitle.c_str(), style,
                CW_USEDEFAULT, CW_USEDEFAULT, outer_w, outer_h,
                nullptr, nullptr, impl->hinstance, impl);

            if (!impl->hwnd) {
                impl->closing = true;
                return impl;
            }

            if (desc.visible) {
                ::ShowWindow(impl->hwnd, SW_SHOW);
                ::UpdateWindow(impl->hwnd);
                impl->visible = true;
            }
            else {
                ::ShowWindow(impl->hwnd, SW_HIDE);
                impl->visible = false;
            }

            return impl;
        }

        void destroy_window_impl(Window::Impl* impl) noexcept {
            if (!impl) return;
            if (impl->clear_brush) {
                ::DeleteObject(impl->clear_brush);
                impl->clear_brush = nullptr;
            }
            if (impl->hwnd) {
                ::DestroyWindow(impl->hwnd);
                impl->hwnd = nullptr;
            }
            delete impl;
        }

        bool should_close_impl(const Window::Impl& impl) noexcept {
            return impl.closing;
        }

        void request_close_impl(Window::Impl& impl) noexcept {
            if (impl.hwnd) {
                ::PostMessageW(impl.hwnd, WM_CLOSE, 0, 0);
            }
            else {
                impl.closing = true;
            }
        }

        void poll_events_impl(Window::Impl&) {
            MSG msg;
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }

        void set_title_impl(Window::Impl& impl, std::string_view title) {
            if (!impl.hwnd) return;
            const std::wstring w = utf8_to_wide(title);
            ::SetWindowTextW(impl.hwnd, w.c_str());
        }

        auto window_size_impl(const Window::Impl& impl) noexcept
            -> std::pair<int, int> {
            if (!impl.hwnd) return { 0, 0 };
            RECT rc{};
            ::GetClientRect(impl.hwnd, &rc);
            return { rc.right - rc.left, rc.bottom - rc.top };
        }

        auto framebuffer_size_impl(const Window::Impl& impl) noexcept
            -> std::pair<int, int> {
            return window_size_impl(impl);
        }

        bool is_minimized_impl(const Window::Impl& impl) noexcept {
            return impl.minimized;
        }

        bool is_visible_impl(const Window::Impl& impl) noexcept {
            return impl.visible;
        }

        auto native_wsi_impl(const Window::Impl& impl) noexcept -> WsiHandle {
            return impl.make_wsi_handle();
        }
    } // namespace detail
} // namespace strata::platform

#endif // defined(_WIN32)
