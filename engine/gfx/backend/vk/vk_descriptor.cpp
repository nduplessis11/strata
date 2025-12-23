// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_descriptor.cpp
//
// Purpose:
//   Implements a Vulkan descriptor pool wrapper that encapsulates pool creation
//   and lifetime management, avoiding out-parameters and enforcing RAII
//   semantics at the backend boundary.
// -----------------------------------------------------------------------------

#include "vk_descriptor.h"

#include <expected>
#include <print>
#include <utility> // std::exchange

namespace strata::gfx::vk
{

// NOTE: If we want these configured by VkGpuDevice or a desc struct,
//       move these constants and pool sizing into the caller.
namespace
{
inline constexpr std::uint32_t max_sets_v1            = 128;
inline constexpr std::uint32_t max_uniform_buffers_v1 = 128;
} // namespace

std::expected<VkDescriptorPoolWrapper, VkResult> VkDescriptorPoolWrapper::create(VkDevice device)
{
    if (device == VK_NULL_HANDLE)
        return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);

    VkDescriptorPoolSize pool_sizes[] = {
        VkDescriptorPoolSize{
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = max_uniform_buffers_v1,
        },
    };

    VkDescriptorPoolCreateInfo const ci{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = max_sets_v1,
        .poolSizeCount = 1,
        .pPoolSizes    = pool_sizes,
    };

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult         res  = vkCreateDescriptorPool(device, &ci, nullptr, &pool);
    if (res != VK_SUCCESS)
    {
        std::println(stderr,
                     "VkDescriptorPoolWrapper: vkCreateDescriptorPool failed({})",
                     static_cast<std::uint32_t>(res));
        return std::unexpected(res);
    }

    return VkDescriptorPoolWrapper{device, pool};
}

VkDescriptorPoolWrapper::VkDescriptorPoolWrapper(VkDescriptorPoolWrapper&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      descriptor_pool_(std::exchange(other.descriptor_pool_, VK_NULL_HANDLE))
{
}

VkDescriptorPoolWrapper& VkDescriptorPoolWrapper::operator=(
    VkDescriptorPoolWrapper&& other) noexcept
{
    if (this == &other)
        return *this;

    // Release current resource first
    if (descriptor_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);

    device_          = std::exchange(other.device_, VK_NULL_HANDLE);
    descriptor_pool_ = std::exchange(other.descriptor_pool_, VK_NULL_HANDLE);

    return *this;
}

VkDescriptorPoolWrapper::~VkDescriptorPoolWrapper() noexcept
{
    if (descriptor_pool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
        device_          = VK_NULL_HANDLE;
    }
}

} // namespace strata::gfx::vk
