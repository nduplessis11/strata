# engine/gfx/CMakeLists.txt
find_package(Vulkan REQUIRED)

add_library(strata_gfx STATIC
  src/graphics_device.cc
  src/renderer2d.cc
  src/backends/vulkan/vulkan_device.h
  src/backends/vulkan/vulkan_device.cc
  src/backends/vulkan/vk_pipeline_basic.h
  src/backends/vulkan/vk_pipeline_basic.cc
  src/backends/vulkan/vulkan_context.cc
  src/backends/vulkan/swapchain.cc

  $<$<PLATFORM_ID:Windows>:src/backends/vulkan/wsi_bridge_win32.cc>
  $<$<AND:$<PLATFORM_ID:Linux>,$<BOOL:${STRATA_USE_X11}>>:src/backends/vulkan/wsi_bridge_x11.cc>
)

target_include_directories(strata_gfx
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(strata_gfx PUBLIC cxx_std_23)
enable_strict_warnings(strata_gfx)
target_link_libraries(strata_gfx PUBLIC engine_options strata_platform Vulkan::Vulkan)

# Vulkan WSI macros localized to this target
if (WIN32)
  target_compile_definitions(strata_gfx PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif (UNIX AND NOT APPLE)
  if (STRATA_USE_X11)
    target_compile_definitions(strata_gfx PRIVATE VK_USE_PLATFORM_XLIB_KHR)
  else()
    target_compile_definitions(strata_gfx PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
  endif()
endif()

# Optional: offline shader compilation with glslc (from Vulkan SDK)
file(GLOB_RECURSE STRATA_GLSL "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.*")
find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
if (GLSLC AND STRATA_GLSL)
  set(SPV_OUT "")
  foreach(GLSL ${STRATA_GLSL})
    get_filename_component(NAME ${GLSL} NAME)
    set(SPV "${CMAKE_CURRENT_BINARY_DIR}/shaders/${NAME}.spv")
    add_custom_command(
      OUTPUT ${SPV}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
      COMMAND ${GLSLC} -std=450 -o ${SPV} ${GLSL}
      DEPENDS ${GLSL}
      COMMENT "Compiling shader ${NAME}"
      VERBATIM
    )
    list(APPEND SPV_OUT ${SPV})
  endforeach()
  add_custom_target(strata_shaders DEPENDS ${SPV_OUT})
  add_dependencies(strata_gfx strata_shaders)
endif()
