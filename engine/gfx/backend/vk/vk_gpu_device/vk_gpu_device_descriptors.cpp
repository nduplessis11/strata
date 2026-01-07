// path: engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_descriptors.cpp
// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device/vk_gpu_device_descriptors.cpp
//
// Purpose:
//   Descriptor set layout creation, descriptor set allocation, and descriptor
//   writes (minimal: uniform buffers only).
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include "../vk_check.h"
#include "strata/base/diagnostics.h"

#include <limits>
#include <vector>

namespace strata::gfx::vk
{

namespace
{

[[nodiscard]]
VkDescriptorType to_vk_descriptor_type(rhi::DescriptorType type)
{
    switch (type)
    {
    case rhi::DescriptorType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    default:
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

[[nodiscard]]
VkShaderStageFlags to_vk_shader_stage_flags(rhi::ShaderStage stages)
{
    VkShaderStageFlags out = 0;

    if ((stages & rhi::ShaderStage::Vertex) != rhi::ShaderStage::NoFlags)
        out |= VK_SHADER_STAGE_VERTEX_BIT;
    if ((stages & rhi::ShaderStage::Fragment) != rhi::ShaderStage::NoFlags)
        out |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if ((stages & rhi::ShaderStage::Compute) != rhi::ShaderStage::NoFlags)
        out |= VK_SHADER_STAGE_COMPUTE_BIT;

    return out;
}

[[nodiscard]]
constexpr VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) noexcept
{
    if (alignment == 0)
        return value;

    VkDeviceSize const rem = value % alignment;
    if (rem == 0)
        return value;

    VkDeviceSize const add = alignment - rem;

    // Overflow-safe (saturate to value if it would overflow).
    // In practice our sizes are small, but this keeps the helper correct.
    if (value > (std::numeric_limits<VkDeviceSize>::max() - add))
        return value;

    return value + add;
}

} // namespace

bool VkGpuDevice::ensure_descriptor_pool()
{
    using namespace strata::base;

    if (!diagnostics_)
        return false;
    auto& diag = *diagnostics_;

    if (descriptor_pool_.has_value())
        return true;

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.desc", "ensure_descriptor_pool failed: device is null");
        return false;
    }

    auto pool_or_err = VkDescriptorPoolWrapper::create(vk_device);
    if (!pool_or_err.has_value())
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.desc",
                         "VkDescriptorPoolWrapper::create failed ({})",
                         static_cast<std::uint32_t>(pool_or_err.error()));
        diag.debug_break_on_error();
        return false;
    }

    descriptor_pool_.emplace(std::move(pool_or_err.value()));
    return true;
}

void VkGpuDevice::cleanup_descriptors()
{
    // IMPORTANT:
    // This function must be called before device_.cleanup().
    //
    // VkGpuDevice::~VkGpuDevice() calls device_.cleanup() in its destructor body.
    // Member destructors run AFTER the destructor body, so any RAII wrappers that
    // destroy Vulkan objects using VkDevice must be reset BEFORE device_.cleanup().

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
    using namespace strata::base;

    if (!diagnostics_)
        return {};
    auto& diag = *diagnostics_;

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.desc",
                         "create_descriptor_set_layout failed: device is null");
        return {};
    }

    // Build Vulkan bindings
    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(desc.bindings.size());

    for (auto const& b : desc.bindings)
    {
        VkDescriptorType const vk_type = to_vk_descriptor_type(b.type);
        STRATA_ASSERT_MSG(diag,
                          vk_type != VK_DESCRIPTOR_TYPE_MAX_ENUM,
                          "Unsupported DescriptorType");

        VkShaderStageFlags const stage_flags = to_vk_shader_stage_flags(b.stages);
        STRATA_ASSERT_MSG(diag, stage_flags != 0, "Descriptor binding has no shader stages");

        if (vk_type == VK_DESCRIPTOR_TYPE_MAX_ENUM || stage_flags == 0)
            return {};

        vk_bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding            = b.binding,
            .descriptorType     = vk_type,
            .descriptorCount    = b.count,
            .stageFlags         = stage_flags,
            .pImmutableSamplers = nullptr,
        });
    }

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<std::uint32_t>(vk_bindings.size());
    ci.pBindings    = vk_bindings.empty() ? nullptr : vk_bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkResult const        res    = vkCreateDescriptorSetLayout(vk_device, &ci, nullptr, &layout);
    if (res != VK_SUCCESS)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.desc",
                         "vkCreateDescriptorSetLayout failed: {}",
                         to_string(res));
        diag.debug_break_on_error();
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
    using namespace strata::base;

    if (!handle)
        return;
    if (!diagnostics_)
        return;
    auto& diag = *diagnostics_;

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
        return;

    std::size_t const index = static_cast<std::size_t>(handle.value - 1);
    if (index >= descriptor_set_layouts_.size())
        return;

    VkDescriptorSetLayout& layout = descriptor_set_layouts_[index];
    if (layout == VK_NULL_HANDLE)
        return;

    // IMPORTANT:
    // If this layout is part of the current pipeline layout recipe,
    // invalidate the backend pipeline + recipe before destroying it.
    for (auto const h : pipeline_set_layout_handles_)
    {
        if (h.value == handle.value)
        {
            STRATA_LOG_WARN(diag.logger(),
                            "vk.desc",
                            "destroy_descriptor_set_layout: layout {} used by current pipeline; "
                            "invalidating pipeline",
                            handle.value);
            basic_pipeline_ = BasicPipeline{};
            pipeline_set_layout_handles_.clear();
            break;
        }
    }

    vkDestroyDescriptorSetLayout(vk_device, layout, nullptr);
    layout = VK_NULL_HANDLE;
}

rhi::DescriptorSetHandle VkGpuDevice::allocate_descriptor_set(rhi::DescriptorSetLayoutHandle layout)
{
    using namespace strata::base;

    if (!diagnostics_)
        return {};
    auto& diag = *diagnostics_;

    STRATA_ASSERT_MSG(diag, layout, "allocate_descriptor_set called with invalid layout");
    if (!layout)
        return {};

    if (!ensure_descriptor_pool())
        return {};

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
        return {};

    VkDescriptorSetLayout const vk_layout = get_vk_descriptor_set_layout(layout);
    if (vk_layout == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.desc",
                         "allocate_descriptor_set failed: layout not found");
        diag.debug_break_on_error();
        return {};
    }

    VkDescriptorPool const pool = descriptor_pool_->descriptor_pool();
    if (pool == VK_NULL_HANDLE)
        return {};

    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &vk_layout;

    VkDescriptorSet vk_set = VK_NULL_HANDLE;
    VkResult const  res    = vkAllocateDescriptorSets(vk_device, &ai, &vk_set);
    if (res != VK_SUCCESS)
    {
        STRATA_LOG_ERROR(diag.logger(),
                         "vk.desc",
                         "vkAllocateDescriptorSets failed: {}",
                         to_string(res));
        diag.debug_break_on_error();
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
    if (!diagnostics_)
        return;

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
        return;

    if (!descriptor_pool_.has_value())
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
        auto& diag = *diagnostics_;
        STRATA_LOG_WARN(diag.logger(),
                        "vk.desc",
                        "vkFreeDescriptorSets failed: {}",
                        to_string(res));
        // Keep going; still invalidate the handle-side entry to avoid double free attempts.
    }

    vk_set = VK_NULL_HANDLE;
}

rhi::FrameResult VkGpuDevice::update_descriptor_set(rhi::DescriptorSetHandle              set,
                                                    std::span<rhi::DescriptorWrite const> writes)
{
    using namespace strata::base;

    if (!diagnostics_)
        return rhi::FrameResult::Error;
    auto& diag = *diagnostics_;

    VkDevice vk_device = device_.device();
    if (vk_device == VK_NULL_HANDLE)
        return rhi::FrameResult::Error;

    VkDescriptorSet const vk_set = get_vk_descriptor_set(set);
    if (vk_set == VK_NULL_HANDLE)
    {
        STRATA_LOG_ERROR(diag.logger(), "vk.desc", "update_descriptor_set failed: set not found");
        diag.debug_break_on_error();
        return rhi::FrameResult::Error;
    }

    if (writes.empty())
        return rhi::FrameResult::Ok;

    // Cache the device limit we need for descriptor offset validation.
    VkDeviceSize           min_ubo_alignment = 0;
    VkPhysicalDevice const physical          = device_.physical();
    if (physical != VK_NULL_HANDLE)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physical, &props);
        min_ubo_alignment = props.limits.minUniformBufferOffsetAlignment;
    }

    std::vector<VkDescriptorBufferInfo> vk_buffer_infos;
    std::vector<VkWriteDescriptorSet>   vk_writes;
    vk_buffer_infos.reserve(writes.size());
    vk_writes.reserve(writes.size());

    for (auto const& w : writes)
    {
        if (w.type != rhi::DescriptorType::UniformBuffer)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.desc",
                             "update_descriptor_set: unsupported DescriptorType");
            diag.debug_break_on_error();
            return rhi::FrameResult::Error;
        }

        VkBuffer const vk_buffer = get_vk_buffer(w.buffer.buffer);
        if (vk_buffer == VK_NULL_HANDLE)
        {
            STRATA_LOG_ERROR(diag.logger(),
                             "vk.desc",
                             "update_descriptor_set: BufferHandle not resolvable");
            diag.debug_break_on_error();
            return rhi::FrameResult::Error;
        }

        VkDeviceSize const offset = static_cast<VkDeviceSize>(w.buffer.offset_bytes);
        VkDeviceSize const range  = (w.buffer.range_bytes == 0)
             ? VK_WHOLE_SIZE
             : static_cast<VkDeviceSize>(w.buffer.range_bytes);

        // Vulkan spec requirement:
        // UNIFORM_BUFFER (and UNIFORM_BUFFER_DYNAMIC) descriptor offsets must be aligned to
        // minUniformBufferOffsetAlignment.
        if (min_ubo_alignment != 0)
        {
            VkDeviceSize const aligned = align_up(offset, min_ubo_alignment);
            if (aligned != offset)
            {
                STRATA_LOG_ERROR(diag.logger(),
                                 "vk.desc",
                                 "update_descriptor_set: uniform buffer offset {} is not aligned "
                                 "to minUniformBufferOffsetAlignment {} (buffer={}, binding={})",
                                 static_cast<std::uint64_t>(offset),
                                 static_cast<std::uint64_t>(min_ubo_alignment),
                                 w.buffer.buffer.value,
                                 w.binding);
                diag.debug_break_on_error();
                return rhi::FrameResult::Error;
            }
        }

        // Extra defensive bounds check against our tracked buffer sizes (when range is explicit).
        if (w.buffer.buffer)
        {
            std::size_t const bindex = static_cast<std::size_t>(w.buffer.buffer.value - 1);
            if (bindex < buffers_.size())
            {
                VkDeviceSize const buf_size =
                    static_cast<VkDeviceSize>(buffers_[bindex].size_bytes);

                if (offset > buf_size)
                {
                    STRATA_LOG_ERROR(diag.logger(),
                                     "vk.desc",
                                     "update_descriptor_set: offset {} exceeds buffer {} size {}",
                                     static_cast<std::uint64_t>(offset),
                                     w.buffer.buffer.value,
                                     static_cast<std::uint64_t>(buf_size));
                    diag.debug_break_on_error();
                    return rhi::FrameResult::Error;
                }

                if (range != VK_WHOLE_SIZE)
                {
                    // Overflow-safe check: offset + range <= buf_size
                    if (range > (buf_size - offset))
                    {
                        STRATA_LOG_ERROR(diag.logger(),
                                         "vk.desc",
                                         "update_descriptor_set: range {} at offset {} exceeds "
                                         "buffer {} size {}",
                                         static_cast<std::uint64_t>(range),
                                         static_cast<std::uint64_t>(offset),
                                         w.buffer.buffer.value,
                                         static_cast<std::uint64_t>(buf_size));
                        diag.debug_break_on_error();
                        return rhi::FrameResult::Error;
                    }
                }
            }
        }

        vk_buffer_infos.push_back(VkDescriptorBufferInfo{
            .buffer = vk_buffer,
            .offset = offset,
            .range  = range,
        });

        VkDescriptorType const vk_type = to_vk_descriptor_type(w.type);
        if (vk_type == VK_DESCRIPTOR_TYPE_MAX_ENUM)
            return rhi::FrameResult::Error;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = vk_set;
        write.dstBinding      = w.binding;
        write.descriptorCount = 1;
        write.descriptorType  = vk_type;
        write.pBufferInfo     = &vk_buffer_infos.back();

        vk_writes.push_back(write);
    }

    vkUpdateDescriptorSets(vk_device,
                           static_cast<std::uint32_t>(vk_writes.size()),
                           vk_writes.data(),
                           0,
                           nullptr);

    return rhi::FrameResult::Ok;
}

} // namespace strata::gfx::vk
