// -----------------------------------------------------------------------------
// engine/gfx/backend/vk/vk_rhi_factory.cpp
//
// Purpose:
//   RHI factory selection for Vulkan.
// -----------------------------------------------------------------------------

#include "vk_gpu_device/vk_gpu_device.h"

namespace strata::gfx::rhi
{
// RHI factory: chooses backend (currently only Vulkan) and forwards to VkGpuDevice.
std::unique_ptr<IGpuDevice> create_device(base::Diagnostics&         diagnostics,
                                          DeviceCreateInfo const&    info,
                                          platform::WsiHandle const& surface)
{
    switch (info.backend)
    {
    case BackendType::Vulkan:
    default:
        return vk::VkGpuDevice::create(diagnostics, info, surface);
    }
}

} // namespace strata::gfx::rhi
