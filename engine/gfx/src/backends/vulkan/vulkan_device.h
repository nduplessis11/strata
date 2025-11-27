#pragma once

#include "strata/gfx/graphics_device.h"
#include "strata/gfx/vulkan/swapchain.h"
#include "strata/gfx/vulkan/vulkan_context.h"
#include "vulkan/vk_pipeline_basic.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

namespace strata::gfx::vulkan {

    class VulkanSwapchain : public GraphicsSwapchain {
    public:
        explicit VulkanSwapchain(Swapchain swapchain);

        strata::platform::Extent2d extent() const override { return swapchain_.extent(); }
        std::uint32_t color_format() const override { return swapchain_.color_format_bits(); }
        bool is_valid() const override { return swapchain_.valid(); }

        VkSwapchainKHR handle() const { return swapchain_.handle(); }
        std::span<const VkImageView> image_views() const { return swapchain_.image_views(); }
        std::span<const VkImage> images() const { return swapchain_.images(); }

    private:
        Swapchain swapchain_;
    };

    class VulkanPipeline : public GraphicsPipeline {
    public:
        explicit VulkanPipeline(vk::BasicPipeline pipeline);
        vk::BasicPipeline pipeline{};
    };

    class VulkanDevice : public GraphicsDevice {
    public:
        VulkanDevice(const strata::platform::WsiHandle& wsi, bool enable_validation);
        ~VulkanDevice() override;

        std::unique_ptr<GraphicsSwapchain> create_swapchain(
            strata::platform::Extent2d size,
            GraphicsSwapchain* old_swapchain) override;

        std::unique_ptr<GraphicsPipeline> create_pipeline(
            const GraphicsSwapchain& swapchain) override;

        FrameResult draw_frame(GraphicsSwapchain& swapchain,
                               GraphicsPipeline* pipeline) override;

        void wait_idle() override;

    private:
        VulkanContext context_;

        struct CommandResources {
            VkDevice        device{ VK_NULL_HANDLE };
            VkCommandPool   pool{ VK_NULL_HANDLE };
            VkCommandBuffer cmd{ VK_NULL_HANDLE };

            CommandResources() = default;
            CommandResources(VkDevice device, std::uint32_t queue_family);
            ~CommandResources();

            CommandResources(const CommandResources&) = delete;
            CommandResources& operator=(const CommandResources&) = delete;
            CommandResources(CommandResources&& other) noexcept;
            CommandResources& operator=(CommandResources&& other) noexcept;
        };

        struct FrameSyncObjects {
            VkDevice    device{ VK_NULL_HANDLE };
            VkSemaphore image_available{ VK_NULL_HANDLE };
            VkSemaphore render_finished{ VK_NULL_HANDLE };
            VkFence     in_flight{ VK_NULL_HANDLE };

            FrameSyncObjects() = default;
            explicit FrameSyncObjects(VkDevice device);
            ~FrameSyncObjects();

            FrameSyncObjects(const FrameSyncObjects&) = delete;
            FrameSyncObjects& operator=(const FrameSyncObjects&) = delete;
            FrameSyncObjects(FrameSyncObjects&& other) noexcept;
            FrameSyncObjects& operator=(FrameSyncObjects&& other) noexcept;
        };

        CommandResources commands_{};
        FrameSyncObjects sync_{};
    };

} // namespace strata::gfx::vulkan
