# engine/gfx/rhi/CMakeLists.txt

add_library(strata_gfx_rhi STATIC
    gpu_types.h
    gpu_device.h
)

# This directory contains the headers (no "include/" nesting here for simplicity).
target_include_directories(strata_gfx_rhi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# RHI depends on platform (for WsiHandle, Extent2d, etc.) and engine_options.
target_link_libraries(strata_gfx_rhi
    PUBLIC
        engine_options
        strata_platform
    PRIVATE
        strata_gfx_backend_vk   # backend is hidden behind the RHI
)

enable_strict_warnings(strata_gfx_rhi)
