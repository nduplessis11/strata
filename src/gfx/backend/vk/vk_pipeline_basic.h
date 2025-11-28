#pragma once

#include <vulkan/vulkan.h>

namespace strata::gfx::vk {

class VkPipelineBasic {
public:
    VkPipelineBasic() = default;
    ~VkPipelineBasic();

    VkPipelineBasic(VkPipelineBasic&&) noexcept;
    VkPipelineBasic& operator=(VkPipelineBasic&&) noexcept;

    bool init(VkDevice device);
    void cleanup(VkDevice device);

    VkPipeline pipeline() const noexcept { return pipeline_; }

private:
    VkPipeline pipeline_{ VK_NULL_HANDLE };
};

} // namespace strata::gfx::vk
