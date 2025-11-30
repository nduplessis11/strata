# engine/gfx/renderer/CMakeLists.txt

add_library(strata_gfx_renderer STATIC
    render_2d.h    render_2d.cpp
    render_scene.h render_scene.cpp
    render_graph.h render_graph.cpp
)

# Public engine headers live under engine/, e.g. "gfx/renderer/render_2d.h".
target_include_directories(strata_gfx_renderer
    PUBLIC
        ${CMAKE_SOURCE_DIR}/engine      # "gfx/renderer/..." and "gfx/rhi/..."
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Renderer depends on platform + RHI, but RHI is PRIVATE so its includes don’t leak.
target_link_libraries(strata_gfx_renderer
    PUBLIC
        engine_options
        strata_platform
    PRIVATE
        strata_gfx_rhi
)

enable_strict_warnings(strata_gfx_renderer)
