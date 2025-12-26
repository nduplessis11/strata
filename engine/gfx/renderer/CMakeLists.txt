# engine/gfx/renderer/CMakeLists.txt

add_library(strata_gfx_renderer STATIC
    src/render_2d.cpp
    src/render_scene.cpp
    src/render_graph.cpp
)

# Public engine headers live under engine/, e.g. "gfx/renderer/render_2d.h".
target_include_directories(strata_gfx_renderer
    PUBLIC
        ${CMAKE_SOURCE_DIR}/engine/gfx/include      # "gfx/renderer/..." and "gfx/rhi/..."
)

target_compile_features(strata_gfx_renderer PUBLIC cxx_std_23)
enable_strict_warnings(strata_gfx_renderer)

# Renderer depends on platform + RHI, but RHI is PRIVATE so its includes don’t leak.
target_link_libraries(strata_gfx_renderer
    PUBLIC
        engine_options
        strata_gfx_rhi
    PRIVATE
        strata_base
)