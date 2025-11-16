// engine/platform/src/win32/window_win32.h
// 
// A skeleton that correctly fills WsiHandle from HWND/HINSTANCE.

#include <windows.h>
#include "strata/platform/window.h"
#include <string>

namespace {
	// Use a unique class name to avoid collisions
	constexpr const wchar_t* kStrataWndClass = L"strata_window_class";

	// Register the window class once per process.
	ATOM register_wnd_class(HINSTANCE hinst, WNDPROC proc) {
		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = proc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = hinst;
		wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
		wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = nullptr;     // no background erase -> less flicker
		wc.lpszMenuName = nullptr;
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
		const int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);

		std::wstring w;
		if (needed > 0) {
			w.resize(needed);
			::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), needed);
		}
		return w;
	}

} // namepace

namespace strata::platform {
	struct Window::Impl {
		HWND hwnd{};
		HINSTANCE hinstance{};
		bool closing{ false };
		bool minimized{ false };

		// Instance handler
		LRESULT wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
			switch (msg) {
			case WM_CLOSE:   closing = true; return 0;
			case WM_DESTROY: ::PostQuitMessage(0); return 0;
			case WM_SIZE:    minimized = (w == SIZE_MINIMIZED); return 0;
			default: break;
			}
			return ::DefWindowProcW(h, msg, w, l);
		}

		// Static thunk: allowed to name Impl
		static LRESULT CALLBACK wndproc_static(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
			if (msg == WM_NCCREATE) {
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
				if (cs && cs->lpCreateParams) {
					::SetWindowLongPtrW(
						hwnd, GWLP_USERDATA,
						reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
				}
			}
			auto* impl = reinterpret_cast<Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			if (impl) return impl->wnd_proc(hwnd, msg, w, l);
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

	Window::Window(const WindowDesc& desc) : p_(std::make_unique<Impl>()) {
		p_->hinstance = ::GetModuleHandleW(nullptr);

		if (!register_wnd_class(p_->hinstance, &Impl::wndproc_static)) {
			p_->closing = true;
			return;
		}

		// Window style + resizable toggle
		DWORD style = WS_OVERLAPPEDWINDOW;
		DWORD ex = WS_EX_APPWINDOW;
		if (!desc.resizable) {
			style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
		}

		// Convert desired client size -> outer rect.
		RECT rect{ 0, 0, desc.size.width, desc.size.height };
		::AdjustWindowRectEx(&rect, style, FALSE, ex);
		const int outer_w = rect.right - rect.left;
		const int outer_h = rect.bottom - rect.top;

		std::wstring wtitle = utf8_to_wide(desc.title);

		// Create the window; pass Impl* via lpCreateParams for WM_NCCREATE.
		HWND hwnd = ::CreateWindowExW(
			ex, kStrataWndClass, wtitle.c_str(), style,
			CW_USEDEFAULT, CW_USEDEFAULT, outer_w, outer_h,
			nullptr, nullptr, p_->hinstance, p_.get());

		if (!hwnd) {
			p_->closing = true;
			return;
		}

		p_->hwnd = hwnd;

		if (desc.visible) {
			::ShowWindow(p_->hwnd, SW_SHOW);
			::UpdateWindow(p_->hwnd);
		}
		else {
			::ShowWindow(p_->hwnd, SW_HIDE);
		}
	}

	Window::~Window() = default;

	Window::Window(Window&&) noexcept = default;
	Window& Window::operator=(Window&&) noexcept = default;

	bool Window::should_close() const noexcept {
		return p_->closing;
	}

	void Window::request_close() noexcept {
		if (p_->hwnd) {
			::PostMessageW(p_->hwnd, WM_CLOSE, 0, 0);
		}
	}

	void Window::poll_events() {
		MSG msg;
		// Process all thread messages (don’t filter to a single HWND).
		while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			::TranslateMessage(&msg);
			::DispatchMessageW(&msg);
		}
	}

	void Window::set_title(std::string_view title) {
		if (!p_->hwnd) return;
		const std::wstring w = utf8_to_wide(title);
		::SetWindowTextW(p_->hwnd, w.c_str());
	}

	auto Window::window_size() const noexcept -> std::pair<int, int> {
		if (!p_->hwnd) return { 0, 0 };
		RECT rc{};
		::GetClientRect(p_->hwnd, &rc);
		const int w = rc.right - rc.left;
		const int h = rc.bottom - rc.top;
		return { w, h };
	}

	auto Window::framebuffer_size() const noexcept -> std::pair<int, int> {
		// For first bring-up, assume client == framebuffer.
		// If you enable per-monitor DPI awareness, compute pixels using GetDpiForWindow().
		return window_size();
	}

	WsiHandle Window::native_wsi() const noexcept {
		return p_->make_wsi_handle();
	}
}