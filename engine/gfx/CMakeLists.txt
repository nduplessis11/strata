# engine/gfx/CMakeLists.txt

add_subdirectory(rhi)
add_subdirectory(renderer)

option(STRATA_ENABLE_VULKAN "Enable Vulkan backend" ON)

if (STRATA_ENABLE_VULKAN)
  add_subdirectory(backend/vk)
endif()

# -----------------------------------------------------------------------------
# Aggregator: what higher layers depend on (no backend mentioned outside gfx)
# -----------------------------------------------------------------------------
add_library(strata_gfx INTERFACE)

target_link_libraries(strata_gfx
  INTERFACE
    strata_gfx_rhi
    strata_gfx_renderer
)

if (STRATA_ENABLE_VULKAN)
  target_link_libraries(strata_gfx INTERFACE strata_gfx_backend_vk)
endif()
