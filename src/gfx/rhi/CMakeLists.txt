add_library(strata_rhi STATIC
    gpu_types.h
    gpu_device.h gpu_device.cpp
)

target_include_directories(strata_rhi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(strata_rhi PUBLIC engine_options strata_platform_new)
