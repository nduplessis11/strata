#include "strata/gfx/graphics_device.h"

#include "backends/vulkan/vulkan_device.h"

namespace strata::gfx {

    std::unique_ptr<GraphicsDevice> GraphicsDevice::create(
        BackendType type,
        const strata::platform::WsiHandle& wsi,
        bool enable_validation) {
        switch (type) {
        case BackendType::Vulkan:
            return std::make_unique<vulkan::VulkanDevice>(wsi, enable_validation);
        }
        return nullptr;
    }

} // namespace strata::gfx
