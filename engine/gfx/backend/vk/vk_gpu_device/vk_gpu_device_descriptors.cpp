// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_descriptors.cpp
//
// Purpose:
//   Descriptor set layout creation, descriptor set allocation, and descriptor
//   writes (minimal: uniform buffers only).
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <print>
#include <vector>

namespace strata::gfx::vk
{
namespace
{

[[nodiscard]] VkDescriptorType to_vk_descriptor_type(rhi::DescriptorType type)
{
    switch (type)
    {
    case rhi::DescriptorType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    default:
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

[[nodiscard]] VkShaderStageFlags to_vk_shader_stage_flags(rhi::ShaderStage stages)
{
    VkShaderStageFlags out = 0;

    if ((stages & rhi::ShaderStage::Vertex) != rhi::ShaderStage::None)
        out |= VK_SHADER_STAGE_VERTEX_BIT;
    if ((stages & rhi::ShaderStage::Fragment) != rhi::ShaderStage::None)
        out |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if ((stages & rhi::ShaderStage::Compute) != rhi::ShaderStage::None)
        out |= VK_SHADER_STAGE_COMPUTE_BIT;

    return out;
}

} // namespace

bool VkGpuDevice::ensure_descriptor_pool()
{
    if (descriptor_pool_.has_value())
        return true;

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
    {
        std::println(stderr, "VkGpuDevice: ensure_descriptor_pool() failed (device is null)");
        return false;
    }

    auto pool_or_err = VkDescriptorPoolWrapper::create(vk_device);
    if (!pool_or_err.has_value())
    {
        std::println(stderr,
                     "VkGpuDevice: VkDescriptorPoolWrapper::create failed({})",
                     static_cast<std::uint32_t>(pool_or_err.error()));
        return false;
    }

    descriptor_pool_.emplace(std::move(pool_or_err.value()));
    return true;
}

void VkGpuDevice::cleanup_descriptors()
{
    // IMPORTANT:
    // VkGpuDevice::~VkGpuDevice() calls device_.cleanup() in its destructor body.
    // Member destructors run AFTER the destructor body, so any RAII wrappers that
    // destroy Vulkan objects using VkDevice must be reset BEFORE device_.cleanup().
    //
    // This function must be called before device_.cleanup().

    VkDevice vk_device = device_.device();

    // Destroy pool first:
    // - Frees all descriptor sets allocated from it.
    // - Ensures VkDescriptorPoolWrapper destructor runs while VkDevice is still valid.
    descriptor_pool_.reset();

    // Our handle tables can now be cleared.
    descriptor_sets_.clear();

    // Descriptor set layouts are separate objects and must be destroyed explicitly.
    if (vk_device != VK_NULL_HANDLE)
    {
        for (auto& layout : descriptor_set_layouts_)
        {
            if (layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(vk_device, layout, nullptr);
                layout = VK_NULL_HANDLE;
            }
        }
    }

    descriptor_set_layouts_.clear();
}

VkDescriptorSetLayout VkGpuDevice::get_vk_descriptor_set_layout(
    rhi::DescriptorSetLayoutHandle handle) const noexcept
{
    if (!handle)
        return VK_NULL_HANDLE;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= descriptor_set_layouts_.size())
        return VK_NULL_HANDLE;

    return descriptor_set_layouts_[index];
}

VkDescriptorSet VkGpuDevice::get_vk_descriptor_set(rhi::DescriptorSetHandle handle) const noexcept
{
    if (!handle)
        return VK_NULL_HANDLE;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= descriptor_sets_.size())
        return VK_NULL_HANDLE;

    return descriptor_sets_[index];
}

rhi::DescriptorSetLayoutHandle VkGpuDevice::create_descriptor_set_layout(
    rhi::DescriptorSetLayoutDesc const& desc)
{
    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
    {
        std::println(stderr, "VkGpuDevice: create_descriptor_set_layout failed (device is null)");
        return {};
    }

    // Build Vulkan bindings
    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(desc.bindings.size());

    for (auto const& b : desc.bindings)
    {
        VkDescriptorType const vk_type = to_vk_descriptor_type(b.type);
        if (vk_type == VK_DESCRIPTOR_TYPE_MAX_ENUM)
        {
            std::println(
                stderr,
                "VkGpuDevice: create_descriptor_set_layout unsupported DescriptorType ({})",
                static_cast<std::uint32_t>(b.type));
            return {};
        }

        VkShaderStageFlags const stage_flags = to_vk_shader_stage_flags(b.stages);
        if (stage_flags == 0)
        {
            std::println(
                stderr,
                "VkGpuDevice: create_descriptor_set_layout binding {} has no shader stages",
                b.binding);
            return {};
        }

        vk_bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding            = b.binding,
            .descriptorType     = vk_type,
            .descriptorCount    = b.count,
            .stageFlags         = stage_flags,
            .pImmutableSamplers = nullptr,
        });
    }

    VkDescriptorSetLayoutCreateInfo const ci{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = nullptr,
        .flags        = 0,
        .bindingCount = static_cast<std::uint32_t>(vk_bindings.size()),
        .pBindings    = vk_bindings.empty() ? nullptr : vk_bindings.data(),
    };

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkResult const        res    = vkCreateDescriptorSetLayout(vk_device, &ci, nullptr, &layout);
    if (res != VK_SUCCESS)
    {
        std::println(stderr,
                     "VkGpuDevice: vkCreateDescriptorSetLayout failed({})",
                     static_cast<std::uint32_t>(res));
        return {};
    }

    rhi::DescriptorSetLayoutHandle const handle = allocate_descriptor_set_layout_handle();
    std::size_t const                    index  = static_cast<std::size_t>(handle.value - 1);

    if (index >= descriptor_set_layouts_.size())
        descriptor_set_layouts_.resize(index + 1, VK_NULL_HANDLE);

    descriptor_set_layouts_[index] = layout;
    return handle;
}

void VkGpuDevice::destroy_descriptor_set_layout(rhi::DescriptorSetLayoutHandle handle)
{
    if (!handle)
        return;

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
        return;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= descriptor_set_layouts_.size())
        return;

    VkDescriptorSetLayout& layout = descriptor_set_layouts_[index];
    if (layout == VK_NULL_HANDLE)
        return;

    vkDestroyDescriptorSetLayout(vk_device, layout, nullptr);
    layout = VK_NULL_HANDLE;
}

rhi::DescriptorSetHandle VkGpuDevice::allocate_descriptor_set(rhi::DescriptorSetLayoutHandle layout)
{
    if (!layout)
    {
        std::println(stderr, "VkGpuDevice: allocate_descriptor_set called with invalid layout");
        return {};
    }

    if (!ensure_descriptor_pool())
        return {};

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
    {
        std::println(stderr, "VkGpuDevice: allocate_descriptor_set failed (device is null)");
        return {};
    }

    VkDescriptorSetLayout const vk_layout = get_vk_descriptor_set_layout(layout);
    if (vk_layout == VK_NULL_HANDLE)
    {
        std::println(stderr, "VkGpuDevice: allocate_descriptor_set failed (layout not found)");
        return {};
    }

    VkDescriptorPool const pool = descriptor_pool_->descriptor_pool();
    if (pool == VK_NULL_HANDLE)
    {
        std::println(stderr, "VkGpuDevice: allocate_descriptor_set failed (pool is null)");
        return {};
    }

    VkDescriptorSetAllocateInfo const ai{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &vk_layout,
    };

    VkDescriptorSet vk_set = VK_NULL_HANDLE;
    VkResult const  res    = vkAllocateDescriptorSets(vk_device, &ai, &vk_set);
    if (res != VK_SUCCESS)
    {
        std::println(stderr,
                     "VkGpuDevice: vkAllocateDescriptorSets failed({})",
                     static_cast<std::uint32_t>(res));
        return {};
    }

    rhi::DescriptorSetHandle const handle = allocate_descriptor_set_handle();
    std::size_t const              index  = static_cast<std::size_t>(handle.value - 1);

    if (index >= descriptor_sets_.size())
        descriptor_sets_.resize(index + 1, VK_NULL_HANDLE);

    descriptor_sets_[index] = vk_set;
    return handle;
}

void VkGpuDevice::free_descriptor_set(rhi::DescriptorSetHandle set)
{
    if (!set)
        return;

    if (!descriptor_pool_.has_value())
        return; // Nothing to free from.

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
        return;

    std::size_t const index = static_cast<std::size_t>(set.value - 1);
    if (index >= descriptor_sets_.size())
        return;

    VkDescriptorSet& vk_set = descriptor_sets_[index];
    if (vk_set == VK_NULL_HANDLE)
        return;

    VkDescriptorPool const pool = descriptor_pool_->descriptor_pool();
    if (pool == VK_NULL_HANDLE)
        return;

    VkResult const res = vkFreeDescriptorSets(vk_device, pool, 1, &vk_set);
    if (res != VK_SUCCESS)
    {
        std::println(stderr,
                     "VkGpuDevice: vkFreeDescriptorSets failed({})",
                     static_cast<std::uint32_t>(res));
        // Keep going; still invalidate the handle-side entry to avoid double free attempts.
    }

    vk_set = VK_NULL_HANDLE;
}

rhi::FrameResult VkGpuDevice::update_descriptor_set(rhi::DescriptorSetHandle              set,
                                                    std::span<rhi::DescriptorWrite const> writes)
{
    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
    {
        std::println(stderr, "VkGpuDevice: update_descriptor_set failed (device is null)");
        return rhi::FrameResult::Error;
    }

    VkDescriptorSet const vk_set = get_vk_descriptor_set(set);
    if (vk_set == VK_NULL_HANDLE)
    {
        std::println(stderr, "VkGpuDevice: update_descriptor_set failed (set not found)");
        return rhi::FrameResult::Error;
    }

    if (writes.empty())
        return rhi::FrameResult::Ok;

    // NOTE:
    // We only support UniformBuffer writes right now.
    // We still need a real BufferHandle -> VkBuffer mapping in the backend to make this useful.
    // If buffers aren't implemented yet, this will return Error until they are.
    //
    // Hook point: replace resolve_vk_buffer(...) with a buffer registry lookup.
    auto resolve_vk_buffer = [&](rhi::BufferHandle buffer_handle) -> VkBuffer
    {
        // TODO: Implement real BufferHandle -> VkBuffer mapping (likely in
        // vk_gpu_device_resources).
        (void)buffer_handle;
        return VK_NULL_HANDLE;
    };

    std::vector<VkDescriptorBufferInfo> vk_buffer_infos;
    std::vector<VkWriteDescriptorSet>   vk_writes;
    vk_buffer_infos.reserve(writes.size());
    vk_writes.reserve(writes.size());

    for (auto const& w : writes)
    {
        if (w.type != rhi::DescriptorType::UniformBuffer)
        {
            std::println(stderr,
                         "VkGpuDevice: update_descriptor_set unsupported DescriptorType ({})",
                         static_cast<std::uint32_t>(w.type));
            return rhi::FrameResult::Error;
        }

        VkBuffer const vk_buffer = resolve_vk_buffer(w.buffer.buffer);
        if (vk_buffer == VK_NULL_HANDLE)
        {
            std::println(
                stderr,
                "VkGpuDevice: update_descriptor_set failed (BufferHandle not resolvable yet)");
            return rhi::FrameResult::Error;
        }

        VkDeviceSize const offset = static_cast<VkDeviceSize>(w.buffer.offset_bytes);
        VkDeviceSize const range  = (w.buffer.range_bytes == 0)
                                        ? VK_WHOLE_SIZE
                                        : static_cast<VkDeviceSize>(w.buffer.range_bytes);

        vk_buffer_infos.push_back(VkDescriptorBufferInfo{
            .buffer = vk_buffer,
            .offset = offset,
            .range  = range,
        });

        VkDescriptorType const vk_type = to_vk_descriptor_type(w.type);
        if (vk_type == VK_DESCRIPTOR_TYPE_MAX_ENUM)
            return rhi::FrameResult::Error;

        vk_writes.push_back(VkWriteDescriptorSet{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = vk_set,
            .dstBinding       = w.binding,
            .dstArrayElement  = 0,
            .descriptorCount  = 1,
            .descriptorType   = vk_type,
            .pImageInfo       = nullptr,
            .pBufferInfo      = &vk_buffer_infos.back(),
            .pTexelBufferView = nullptr,
        });
    }

    vkUpdateDescriptorSets(vk_device,
                           static_cast<std::uint32_t>(vk_writes.size()),
                           vk_writes.data(),
                           0,
                           nullptr);

    return rhi::FrameResult::Ok;
}

} // namespace strata::gfx::vk
