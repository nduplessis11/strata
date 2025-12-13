# engine/gfx/rhi/CMakeLists.txt

# Header-only RHI: no .lib file, just usage requirements.
add_library(strata_gfx_rhi INTERFACE)

# Only expose the public gfx include tree.
target_include_directories(strata_gfx_rhi
    INTERFACE
        ${CMAKE_SOURCE_DIR}/engine/gfx/include
)

# RHI depends on engine_options + platform types, and (currently) the active backend for create_device().
target_link_libraries(strata_gfx_rhi
    INTERFACE
        engine_options
        strata_platform
        strata_gfx_backend_vk
)
