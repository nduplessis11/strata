#include <windows.h>
#include "strata/platform/window.h"

namespace strata::platform {
	struct Window::Impl {
		HWND hwnd{};
		HINSTANCE hinstance{};

		WsiHandle make_wsi_handle() const {
			using namespace wsi;
			Win32 h{};
			h.instance.value = reinterpret_cast<std::uintptr_t>(hinstance);
			h.window.value = reinterpret_cast<std::uintptr_t>(hwnd);
			return WsiHandle{ std::in_place_type<Win32>, h };
		}
	};

	Window::Window(const WindowDesc& desc) : p_(std::make_unique<Impl>()) {
		// create HWND etc.
	}

	Window::~Window() = default;

	bool Window::should_close() const noexcept {
		// use p_->hwnd and Win32 message pump
	}

	WsiHandle Window::native_wsi() const noexcept {
		return p_->make_wsi_handle();
	}
}