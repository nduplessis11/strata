# engine/gfx/CMakeLists.txt

add_subdirectory(rhi)
add_subdirectory(backend/vk)
add_subdirectory(renderer)

# -----------------------------------------------------------------------------
# Aggregator: what higher layers depend on (no backend mentioned outside gfx)
# -----------------------------------------------------------------------------
add_library(strata_gfx INTERFACE)

target_link_libraries(strata_gfx
  INTERFACE
    strata_gfx_rhi
    strata_gfx_renderer
    strata_gfx_backend_vk   # selected backend (for now)
)