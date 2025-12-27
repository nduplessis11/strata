// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_command_buffer.cpp
//
// Purpose:
//   Implement Vulkan command buffer pool utilities.
// -----------------------------------------------------------------------------

#include "vk_command_buffer.h"

#include "strata/base/diagnostics.h"
#include "vk_check.h"

#include <source_location>

namespace strata::gfx::vk
{

namespace
{

void log_vk_error(base::Diagnostics* diag, char const* what, VkResult r)
{
    if (!diag)
        return;

    diag->logger().log(base::LogLevel::Error,
                       "vk.cmd",
                       vk_error_message(what, r),
                       std::source_location::current());
    diag->debug_break_on_error();
}

} // namespace

VkCommandBufferPool::~VkCommandBufferPool() = default;

VkCommandBufferPool::VkCommandBufferPool(VkCommandBufferPool&& other) noexcept
{
    diagnostics_ = other.diagnostics_;
    pool_        = other.pool_;

    other.diagnostics_ = nullptr;
    other.pool_        = VK_NULL_HANDLE;
}

VkCommandBufferPool& VkCommandBufferPool::operator=(VkCommandBufferPool&& other) noexcept
{
    if (this != &other)
    {
        diagnostics_ = other.diagnostics_;
        pool_        = other.pool_;

        other.diagnostics_ = nullptr;
        other.pool_        = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkCommandBufferPool::init(VkDevice device, std::uint32_t queue_family_index)
{
    if (device == VK_NULL_HANDLE)
    {
        if (diagnostics_)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "vk.cmd",
                             "VkCommandBufferPool::init called with VK_NULL_HANDLE device");
        }
        pool_ = VK_NULL_HANDLE;
        return false;
    }

    // In case we're reinitializing
    cleanup(device);

    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = queue_family_index;

    VkResult const r = vkCreateCommandPool(device, &ci, nullptr, &pool_);
    if (r != VK_SUCCESS)
    {
        pool_ = VK_NULL_HANDLE;
        log_vk_error(diagnostics_, "vkCreateCommandPool", r);
        return false;
    }

    if (diagnostics_)
    {
        STRATA_LOG_DEBUG(diagnostics_->logger(),
                         "vk.cmd",
                         "Command pool created (family {})",
                         queue_family_index);
    }
    return true;
}

void VkCommandBufferPool::cleanup(VkDevice device)
{
    if (pool_ == VK_NULL_HANDLE)
        return;

    if (device == VK_NULL_HANDLE)
    {
        if (diagnostics_)
        {
            STRATA_LOG_WARN(diagnostics_->logger(),
                            "vk.cmd",
                            "VkCommandBufferPool::cleanup called with VK_NULL_HANDLE device; "
                            "leaking VkCommandPool");
        }
        pool_ = VK_NULL_HANDLE;
        return;
    }

    vkDestroyCommandPool(device, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
}

VkCommandBuffer VkCommandBufferPool::allocate(VkDevice device)
{
    if (pool_ == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd{VK_NULL_HANDLE};
    VkResult const  r = vkAllocateCommandBuffers(device, &ai, &cmd);
    if (r != VK_SUCCESS)
    {
        log_vk_error(diagnostics_, "vkAllocateCommandBuffers", r);
        return VK_NULL_HANDLE;
    }
    return cmd;
}

} // namespace strata::gfx::vk
