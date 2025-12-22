// -----------------------------------------------------------------------------
// engine/platform/include/strata/platform/window.h
//
// Purpose:
//   Public RAII + pImpl window class. Owns the native window, exposes portable
//   operations, and hands the renderer a WsiHandle for graphics integration.
// -----------------------------------------------------------------------------

#pragma once
#include "strata/platform/wsi_handle.h"
#include <memory>
#include <string_view>

namespace strata::platform
{
struct Extent2d
{
    std::int32_t width{};
    std::int32_t height{};
};

struct WindowDesc
{
    Extent2d         size{1280, 720};
    std::string_view title{"strata"};
    bool             resizable{true};
    bool             visible{true};
};

class Window
{
  public:
    explicit Window(WindowDesc const& desc);
    ~Window();

    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    Window(Window const&)            = delete;
    Window& operator=(Window const&) = delete;

    [[nodiscard]] bool should_close() const noexcept;
    void               request_close() noexcept;

    void poll_events();
    void set_title(std::string_view title);

    // size queries
    [[nodiscard]] auto window_size() const noexcept -> std::pair<std::int32_t,
                                                                 std::int32_t>;
    [[nodiscard]] auto framebuffer_size() const noexcept -> std::pair<std::int32_t,
                                                                      std::int32_t>;

    bool is_minimized() const noexcept;
    bool is_visible() const noexcept;

    // access to native handles in a strongly-typed variant
    [[nodiscard]] auto native_wsi() const noexcept -> WsiHandle;

  private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace strata::platform