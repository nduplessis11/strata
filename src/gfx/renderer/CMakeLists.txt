add_library(strata_renderer STATIC
    render_2d.h render_2d.cpp
    render_scene.h render_scene.cpp
    render_graph.h render_graph.cpp
)

target_include_directories(strata_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(strata_renderer PUBLIC strata_rhi)
