#include "gfx/rhi/gpu_device.h"

#include "gfx/backend/vk/vk_gpu_device.h"

namespace strata::gfx::rhi {

std::unique_ptr<IGpuDevice> create_device(
    const DeviceCreateInfo& info,
    const strata::platform::WsiHandle& surface)
{
    switch (info.backend) {
    case BackendType::Vulkan:
    default:
        return vk::VkGpuDevice::create(info, surface);
    }
}

} // namespace strata::gfx::rhi
