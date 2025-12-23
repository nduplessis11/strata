// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_gpu_device_descriptors.cpp
//
// Purpose:
//   Descriptor set allocation and updates (stubbed).
// -----------------------------------------------------------------------------

#include "vk_gpu_device.h"

#include <print>

namespace strata::gfx::vk
{

rhi::DescriptorSetLayoutHandle VkGpuDevice::create_descriptor_set_layout(
    rhi::DescriptorSetLayoutDesc const& /*desc*/)
{
    // Stub
    std::println(stderr, "VkGpuDevice: create_descriptor_set_layout not implemented");
    return {};
}

void VkGpuDevice::destroy_descriptor_set_layout(rhi::DescriptorSetLayoutHandle /*handle*/)
{
    // Stub
    std::println(stderr, "VkGpuDevice: destroy_descriptor_set_layout not implemented");
}

rhi::DescriptorSetHandle VkGpuDevice::allocate_descriptor_set(
    rhi::DescriptorSetLayoutHandle /*layout*/)
{
    // Stub
    std::println(stderr, "VkGpuDevice: allocate_descriptor_set not implemented");
    return {};
}

void VkGpuDevice::free_descriptor_set(rhi::DescriptorSetHandle /*set*/) {}

rhi::FrameResult VkGpuDevice::update_descriptor_set(
    rhi::DescriptorSetHandle /*set*/,
    std::span<rhi::DescriptorWrite const> /*writes*/)
{
    // Stub
    std::println(stderr, "VkGpuDevice: free_descriptor_set not implemented");
    return rhi::FrameResult::Error;
}

} // namespace strata::gfx::vk
