// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_descriptor.h
//
// Purpose:
//   Declare a Vulkan descriptor pool wrapper.
// -----------------------------------------------------------------------------

#pragma once

#include <expected>
#include <vulkan/vulkan.h>

namespace strata::gfx::vk
{

class VkDescriptorPoolWrapper
{
  public:
    static std::expected<VkDescriptorPoolWrapper, VkResult> create(VkDevice device);

    VkDescriptorPoolWrapper(VkDescriptorPoolWrapper const&)            = delete;
    VkDescriptorPoolWrapper& operator=(VkDescriptorPoolWrapper const&) = delete;

    VkDescriptorPoolWrapper(VkDescriptorPoolWrapper&& other) noexcept;
    VkDescriptorPoolWrapper& operator=(VkDescriptorPoolWrapper&& other) noexcept;

    ~VkDescriptorPoolWrapper() noexcept;

    [[nodiscard]] VkDescriptorPool descriptor_pool() const noexcept
    {
        return descriptor_pool_;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return descriptor_pool_ != VK_NULL_HANDLE;
    }

  private:
    VkDescriptorPoolWrapper(VkDevice device, VkDescriptorPool descriptor_pool) noexcept
        : device_(device), descriptor_pool_(descriptor_pool)
    {
    }

    VkDevice         device_{VK_NULL_HANDLE}; // non-owning; used for destruction
    VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
};

} // namespace strata::gfx::vk
