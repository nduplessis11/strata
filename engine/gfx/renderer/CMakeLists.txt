# engine/gfx/renderer/CMakeLists.txt

add_library(strata_gfx_renderer STATIC
    render_2d.h    render_2d.cpp
    render_scene.h render_scene.cpp
    render_graph.h render_graph.cpp
)

# Public headers for the engine live here:
# e.g. engine/gfx/renderer/render_2d.h -> #include "strata/gfx/render_2d.h"
target_include_directories(strata_gfx_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}   # expose renderer headers to the engine
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Renderer depends on platform + RHI, but RHI is PRIVATE so its includes don�t leak.
target_link_libraries(strata_gfx_renderer
    PUBLIC
        engine_options
        strata_platform
    PRIVATE
        strata_gfx_rhi
)

enable_strict_warnings(strata_gfx_renderer)
