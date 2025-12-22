// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/rhi/gpu_device.h
//
// Purpose:
//   Define the RHI IGpuDevice interface and backend factory.
// -----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <span>

#include "gpu_types.h"
#include "strata/platform/wsi_handle.h" // for surface creation

namespace strata::gfx::rhi
{

class IGpuDevice
{
  public:
    virtual ~IGpuDevice() = default;

    // --- Swapchain -----------------------------------------------------------
    virtual SwapchainHandle create_swapchain(SwapchainDesc const&       desc,
                                             platform::WsiHandle const& surface)               = 0;
    virtual FrameResult resize_swapchain(SwapchainHandle swapchain, SwapchainDesc const& desc) = 0;
    virtual FrameResult acquire_next_image(SwapchainHandle swapchain, AcquiredImage& out)      = 0;
    virtual FrameResult present(SwapchainHandle swapchain, std::uint32_t image_index)          = 0;

    // --- Buffers -------------------------------------------------------------
    virtual BufferHandle create_buffer(BufferDesc const&          desc,
                                       std::span<std::byte const> initial_data = {}) = 0;
    virtual void         destroy_buffer(BufferHandle handle)                         = 0;

    // --- Textures ------------------------------------------------------------
    virtual TextureHandle create_texture(TextureDesc const& desc) = 0;
    virtual void          destroy_texture(TextureHandle handle)   = 0;

    // --- Pipelines -----------------------------------------------------------
    virtual PipelineHandle create_pipeline(PipelineDesc const& desc) = 0;
    virtual void           destroy_pipeline(PipelineHandle handle)   = 0;

    // --- Commands & submission ----------------------------------------------
    virtual CommandBufferHandle begin_commands()                      = 0;
    virtual FrameResult         end_commands(CommandBufferHandle cmd) = 0;

    struct SubmitDesc
    {
        CommandBufferHandle command_buffer{};
        SwapchainHandle     swapchain{};
        std::uint32_t       image_index{};
        std::uint32_t       frame_index{};
    };
    virtual FrameResult submit(SubmitDesc const& submit) = 0;

    // --- Descriptor sets ---------------------------------------------------------
    virtual DescriptorSetLayoutHandle create_descriptor_set_layout(
        DescriptorSetLayoutDesc const& desc)                                     = 0;
    virtual void destroy_descriptor_set_layout(DescriptorSetLayoutHandle handle) = 0;

    virtual DescriptorSetHandle allocate_descriptor_set(DescriptorSetLayoutHandle layout) = 0;
    virtual void                free_descriptor_set(DescriptorSetHandle set)              = 0;

    virtual FrameResult update_descriptor_set(DescriptorSetHandle              set,
                                              std::span<DescriptorWrite const> writes) = 0;

    // --- Recording (explicit functions fine for now)
    // -------------------------------------------------------------
    // TODO: turn these into a CommandList/Encoder object later for nicer API

    // Bind a descriptor set for a pipeline at a given set index (0 = first set)
    virtual FrameResult cmd_bind_descriptor_set(CommandBufferHandle cmd,
                                                PipelineHandle      pipeline,
                                                std::uint32_t       set_index,
                                                DescriptorSetHandle set) = 0;

    virtual FrameResult cmd_begin_swapchain_pass(CommandBufferHandle cmd,
                                                 SwapchainHandle     swapchain,
                                                 std::uint32_t       image_index,
                                                 ClearColor const&   clear)                   = 0;
    virtual FrameResult cmd_end_swapchain_pass(CommandBufferHandle cmd,
                                               SwapchainHandle     swapchain,
                                               std::uint32_t       image_index)                   = 0;
    virtual FrameResult cmd_bind_pipeline(CommandBufferHandle cmd, PipelineHandle pipeline) = 0;
    virtual FrameResult cmd_set_viewport_scissor(CommandBufferHandle cmd, Extent2D extent)  = 0;
    virtual FrameResult cmd_draw(CommandBufferHandle cmd,
                                 std::uint32_t       vertex_count,
                                 std::uint32_t       instance_count,
                                 std::uint32_t       first_vertex,
                                 std::uint32_t       first_instance)                              = 0;

    virtual void wait_idle() = 0;
};

// Factory for the active backend (for now, Vulkan only).
enum class BackendType
{
    Vulkan,
    // D3D12, OpenGL, etc. later
};

struct DeviceCreateInfo
{
    BackendType backend{BackendType::Vulkan};
    // Frames in flight, debugging flags, etc., can go here
};

std::unique_ptr<IGpuDevice> create_device(DeviceCreateInfo const&    info,
                                          platform::WsiHandle const& surface);

} // namespace strata::gfx::rhi
