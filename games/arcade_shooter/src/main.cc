#include "strata/platform/window.h"
#include "strata/platform/wsi_handle.h"

#include <chrono>
#include <thread>
#include <print>
#include <type_traits>

int main() {
	using namespace strata::platform;

	// Describe the window we want.
	WindowDesc desc;
	desc.size = { 1280, 720 };
	desc.title = "strata - window smoke";
	desc.visible = true;
	desc.resizable = true;

	// Create it (RAII). If creation failed, should_close() will be true.
	Window win{ desc };
	if (win.should_close()) {
		std::println(stderr, "Failed to create window");
		return 1;
	}

	// WsiHandle is a std::variant<wsi::Win32, wsi::X11, wsi::Wayland>.
	// A variant can hold EXACTLY ONE of these types at runtime.
	const WsiHandle wsi = win.native_wsi();

	// ------------------------------
	// HOW std::visit WORKS:
	//
	// This lambda:
	//      [](auto const& info) { ... }
	//
	// is a *generic lambda*, meaning it is secretly:
	//
	//      template<class U>
	//      void operator()(U const& info) const;
	//
	// std::visit sees that the variant has 3 possible types, so it
	// instantiates THREE DIFFERENT VERSIONS of this templated operator():
	//
	//   operator()<wsi::Win32>(wsi::Win32 const&)
	//   operator()<wsi::X11>(wsi::X11 const&)
	//   operator()<wsi::Wayland>(wsi::Wayland const&)
	//
	// These three instantiations are generated at COMPILE TIME.
	//
	// Later, at RUNTIME, visit() simply chooses which of the 3 compiled
	// functions to call, depending on what the variant holds.
	// ------------------------------
	std::visit([](auto const& info) {

		// decltype(info) is:
		//   - wsi::Win32 const&   in the Win32 instantiation
		//   - wsi::X11  const&    in the X11 instantiation
		//   - wsi::Wayland const& in the Wayland instantiation
		//
		// But we want the BASE type, not "const&".
		//
		// std::decay_t removes references and const,
		// giving the clean type used in the variant:
		//   - wsi::Win32
		//   - wsi::X11
		//   - wsi::Wayland
		//
		// IMPORTANT:
		//     T is a COMPILE-TIME TYPE because this whole function body
		//     is being INSTANTIATED specifically for one "U".
		using T = std::decay_t<decltype(info)>;

		// ------------------------------
		// HOW if constexpr WORKS HERE:
		//
		// Because T is a compile-time type, each instantiation sees
		// a DIFFERENT literal type for T:
		//
		//   Instantiation 1: T = wsi::Win32
		//   Instantiation 2: T = wsi::X11
		//   Instantiation 3: T = wsi::Wayland
		//
		// The compiler evaluates each `if constexpr` USING THAT T,
		// which means the OTHER branches become `constexpr(false)`
		// and are COMPLETELY REMOVED FROM THAT INSTANTIATION.
		//
		// So inside the Win32 version, ONLY the Win32 code exists.
		// So inside the X11 version, ONLY the X11 code exists.
		// So inside the Wayland version, ONLY the Wayland code exists.
		//
		// No runtime type-checking occurs here—it's PURE compile time.
		// ------------------------------
		if constexpr (std::is_same_v<T, wsi::Win32>) {

			// info.window.value is std::uintptr_t, so {:016X} formats it as:
			//   - pad with 0's
			//   - 16 hex digits
			//   - uppercase A-F
			std::println("WSI: Win32 hwnd={:016X} hinstance={:016X}",
				info.window.value,
				info.instance.value);
		}
		else if constexpr (std::is_same_v<T, wsi::X11>) {
			std::println("WSI: X11 display={:016X} window={}",
				info.display.value,
				info.window.value);
		}
		else { // Wayland
			std::println("WSI: Wayland display={:016X} surface={:016X}",
				info.display.value,
				info.surface.value);
		}
		}, wsi);

	// Main loop: pump events until the user closes the window.
	while (!win.should_close()) {
		win.poll_events();

		// Print size changes
		static int last_w = -1, last_h = -1;

		auto [w, h] = win.window_size();

		if (w != last_w || h != last_h) {
			std::println("client size: {}x{}", w, h);
			last_w = w; last_h = h;
		}

		// Keep CPU reasonable for a no-render loop.
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return 0;
}
