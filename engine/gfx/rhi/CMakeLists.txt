# engine/gfx/rhi/CMakeLists.txt

# Header-only RHI: no .lib file, just usage requirements.
add_library(strata_gfx_rhi INTERFACE)

# Expose the engine/ tree and this folder to any user of the RHI.
target_include_directories(strata_gfx_rhi
    INTERFACE
        ${CMAKE_SOURCE_DIR}/engine
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# RHI depends on engine_options, platform, and backend.
target_link_libraries(strata_gfx_rhi
    INTERFACE
        engine_options
        strata_platform
        strata_gfx_backend_vk
)