#pragma once
#include <memory>
#include <string_view>
#include "strata/platform/wsi_handle.h"

namespace strata::platform {
	struct Extent2d {
		int width{};
		int height{};
	};

	struct WindowDesc {
		Extent2d         size{ 1280, 720 };
		std::string_view title{ "strata" };
		bool             resizable{ true };
		bool             visible{ true };
	};

	class Window {
	public:
		explicit Window(const WindowDesc& desc);
		~Window();

		Window(Window&&) noexcept;
		Window& operator=(Window&&) noexcept;

		bool should_close() const noexcept;
		void request_close() noexcept;

		void poll_events();
		void set_title(std::string_view title);

		// size queries
		auto window_size() const noexcept -> std::pair<int, int>;
		auto framebuffer_size() const noexcept -> std::pair<int, int>;

		// access to native handles in a strongly-typed variant
		auto native_wsi() const noexcept -> WsiHandle;

	private:
		struct Impl;
		std::unique_ptr<Impl> p_;
	};
}