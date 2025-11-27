#pragma once

#include <cstdint>
#include <memory>

#include "strata/platform/window.h"
#include "strata/platform/wsi_handle.h"

namespace strata::gfx {

    enum class BackendType {
        Vulkan,
    };

    enum class FrameResult {
        Ok,
        SwapchainOutOfDate,
        Error,
    };

    class GraphicsSwapchain;
    class GraphicsPipeline;

    class GraphicsDevice {
    public:
        virtual ~GraphicsDevice() = default;

        static std::unique_ptr<GraphicsDevice> create(
            BackendType type,
            const strata::platform::WsiHandle& wsi,
            bool enable_validation = false);

        virtual std::unique_ptr<GraphicsSwapchain> create_swapchain(
            strata::platform::Extent2d size,
            GraphicsSwapchain* old_swapchain = nullptr) = 0;

        virtual std::unique_ptr<GraphicsPipeline> create_pipeline(
            const GraphicsSwapchain& swapchain) = 0;

        virtual FrameResult draw_frame(GraphicsSwapchain& swapchain,
                                       GraphicsPipeline* pipeline = nullptr) = 0;

        virtual void wait_idle() = 0;
    };

    class GraphicsSwapchain {
    public:
        virtual ~GraphicsSwapchain() = default;

        virtual strata::platform::Extent2d extent() const = 0;
        virtual std::uint32_t color_format() const = 0;
        virtual bool is_valid() const = 0;
    };

    class GraphicsPipeline {
    public:
        virtual ~GraphicsPipeline() = default;
    };

} // namespace strata::gfx
