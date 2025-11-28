add_library(strata_rhi STATIC
    gpu_types.h
    gpu_device.h gpu_device.cpp
)

# Vulkan headers are needed to build the Vulkan-backed factory in gpu_device.cpp.
find_package(Vulkan QUIET)
if (Vulkan_FOUND)
    target_include_directories(strata_rhi PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

target_include_directories(strata_rhi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(strata_rhi PUBLIC engine_options strata_platform_new)
