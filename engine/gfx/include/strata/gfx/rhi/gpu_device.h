// engine/gfx/include/strata/gfx/rhi/gpu_device.h
#pragma once

#include <memory>
#include <span>

#include "gpu_types.h"
#include "strata/platform/wsi_handle.h"   // for surface creation

namespace strata::gfx::rhi {

    class IGpuDevice {
    public:
        virtual ~IGpuDevice() = default;

        // --- Swapchain -----------------------------------------------------------
        virtual SwapchainHandle create_swapchain(const SwapchainDesc& desc, const strata::platform::WsiHandle& surface) = 0;
        virtual FrameResult     resize_swapchain(SwapchainHandle swapchain,const SwapchainDesc& desc) = 0;
        virtual FrameResult     acquire_next_image(SwapchainHandle swapchain, AcquiredImage& out) = 0;
        virtual FrameResult     present(SwapchainHandle swapchain, std::uint32_t image_index) = 0;

        // --- Buffers -------------------------------------------------------------
        virtual BufferHandle create_buffer(const BufferDesc& desc, std::span<const std::byte> initial_data = {}) = 0;
        virtual void         destroy_buffer(BufferHandle handle) = 0;

        // --- Textures ------------------------------------------------------------
        virtual TextureHandle create_texture(const TextureDesc& desc) = 0;
        virtual void          destroy_texture(TextureHandle handle) = 0;

        // --- Pipelines -----------------------------------------------------------
        virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;
        virtual void           destroy_pipeline(PipelineHandle handle) = 0;

        // --- Commands & submission ----------------------------------------------
        virtual CommandBufferHandle begin_commands() = 0;
        virtual FrameResult         end_commands(CommandBufferHandle cmd) = 0;

        struct SubmitDesc {
            CommandBufferHandle command_buffer{};
            SwapchainHandle     swapchain{};
            std::uint32_t       image_index{};
            std::uint32_t       frame_index{};
        };
        virtual FrameResult submit(const SubmitDesc& submit) = 0;

        // --- Recording (explicit functions fine for now) -------------------------------------------------------------
        // TODO: turn these into a CommandList/Encoder object later for nicer API

        virtual FrameResult cmd_begin_swapchain_pass(CommandBufferHandle cmd, SwapchainHandle swapchain, std::uint32_t image_index, const ClearColor& clear) = 0;
        virtual FrameResult cmd_end_swapchain_pass(CommandBufferHandle cmd, SwapchainHandle swapchain, std::uint32_t image_index) = 0;
        virtual FrameResult cmd_bind_pipeline(CommandBufferHandle cmd, PipelineHandle pipeline) = 0;
        virtual FrameResult cmd_set_viewport_scissor(CommandBufferHandle cmd, Extent2D extent) = 0;
        virtual FrameResult cmd_draw(CommandBufferHandle cmd, std::uint32_t vertex_count, std::uint32_t instance_count, std::uint32_t first_vertex, std::uint32_t first_instance) = 0;

        virtual void wait_idle() = 0;
    };

    // Factory for the active backend (for now, Vulkan only).
    enum class BackendType {
        Vulkan,
        // D3D12, OpenGL, etc. later
    };

    struct DeviceCreateInfo {
        BackendType backend{ BackendType::Vulkan };
        // Frames in flight, debugging flags, etc., can go here
    };

    std::unique_ptr<IGpuDevice> create_device(const DeviceCreateInfo& info, const strata::platform::WsiHandle& surface);

} // namespace strata::gfx::rhi
