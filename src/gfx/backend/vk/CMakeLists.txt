find_package(Vulkan QUIET)

if (NOT Vulkan_FOUND)
    message(WARNING "Vulkan SDK not found; skipping strata_rhi_vk backend")
    return()
endif()

add_library(strata_rhi_vk STATIC
    vk_instance.h vk_instance.cpp
    vk_device.h vk_device.cpp
    vk_swapchain.h vk_swapchain.cpp
    vk_command_buffer.h vk_command_buffer.cpp
    vk_gpu_device.h vk_gpu_device.cpp
    vk_pipeline_basic.h vk_pipeline_basic.cpp
)

target_include_directories(strata_rhi_vk
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(strata_rhi_vk
    PUBLIC
        strata_rhi
        Vulkan::Vulkan
)

set(STRATA_BUILD_RHI_VK ON PARENT_SCOPE)
