# engine/gfx/CMakeLists.txt

add_subdirectory(rhi)
add_subdirectory(renderer)

option(STRATA_ENABLE_VULKAN "Enable Vulkan backend" ON)

if (NOT STRATA_ENABLE_VULKAN)
  message(FATAL_ERROR
    "STRATA_ENABLE_VULKAN=OFF is not supported yet because Vulkan is the only gfx backend. "
    "Set STRATA_ENABLE_VULKAN=ON.")
endif()

add_subdirectory(backend/vk)


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
