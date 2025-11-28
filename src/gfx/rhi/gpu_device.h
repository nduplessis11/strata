#pragma once

#include <memory>
#include <span>

#include "gfx/rhi/gpu_types.h"
#include "platform/wsi_handle.h"

namespace strata::gfx::rhi {

struct SubmitDesc {
    CommandBufferHandle command_buffer{};
};

class IGpuDevice {
public:
    virtual ~IGpuDevice() = default;

    virtual SwapchainHandle create_swapchain(const SwapchainDesc& desc,
                                             const strata::platform::WsiHandle& surface) = 0;
    virtual FrameResult     present(SwapchainHandle swapchain) = 0;
    virtual FrameResult     resize_swapchain(SwapchainHandle swapchain,
                                             const SwapchainDesc& desc) = 0;

    virtual BufferHandle create_buffer(const BufferDesc& desc,
                                       std::span<const std::byte> initial_data = {}) = 0;
    virtual void         destroy_buffer(BufferHandle handle) = 0;

    virtual TextureHandle create_texture(const TextureDesc& desc) = 0;
    virtual void          destroy_texture(TextureHandle handle) = 0;

    virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;
    virtual void           destroy_pipeline(PipelineHandle handle) = 0;

    virtual CommandBufferHandle begin_commands() = 0;
    virtual void                end_commands(CommandBufferHandle cmd) = 0;
    virtual void                submit(const SubmitDesc& submit) = 0;

    virtual void wait_idle() = 0;
};

enum class BackendType {
    Vulkan,
};

struct DeviceCreateInfo {
    BackendType backend{ BackendType::Vulkan };
};

std::unique_ptr<IGpuDevice> create_device(
    const DeviceCreateInfo& info,
    const strata::platform::WsiHandle& surface);

} // namespace strata::gfx::rhi
