#include "gfx/backend/vk/vk_pipeline_basic.h"

namespace strata::gfx::vk {

VkPipelineBasic::~VkPipelineBasic() = default;

VkPipelineBasic::VkPipelineBasic(VkPipelineBasic&& other) noexcept {
    pipeline_ = other.pipeline_;
    other.pipeline_ = VK_NULL_HANDLE;
}

VkPipelineBasic& VkPipelineBasic::operator=(VkPipelineBasic&& other) noexcept {
    if (this != &other) {
        pipeline_ = other.pipeline_;
        other.pipeline_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkPipelineBasic::init(VkDevice) {
    // Stub: real implementation would build a graphics pipeline
    pipeline_ = VK_NULL_HANDLE;
    return true;
}

void VkPipelineBasic::cleanup(VkDevice) {
    pipeline_ = VK_NULL_HANDLE;
}

} // namespace strata::gfx::vk
