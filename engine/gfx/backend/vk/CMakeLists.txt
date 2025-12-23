# engine/gfx/backend/vk/CMakeLists.txt

find_package(Vulkan REQUIRED)

add_library(strata_gfx_backend_vk STATIC
    vk_instance.cpp
    vk_device.cpp
    vk_swapchain.cpp
    vk_command_buffer.cpp
    vk_rhi_factory.cpp
    vk_gpu_device/vk_gpu_device.cpp
    vk_gpu_device/vk_gpu_device_swapchain.cpp
    vk_gpu_device/vk_gpu_device_submission.cpp
    vk_gpu_device/vk_gpu_device_recording.cpp
    vk_gpu_device/vk_gpu_device_descriptors.cpp
    vk_gpu_device/vk_gpu_device_resources.cpp
    vk_gpu_device/vk_gpu_device_pipeline.cpp
    vk_pipeline_basic.cpp
    $<$<PLATFORM_ID:Windows>:vk_wsi_bridge_win32.cpp>
    $<$<AND:$<PLATFORM_ID:Linux>,$<BOOL:${STRATA_USE_X11}>>:vk_wsi_bridge_x11.cpp>
)

# Toggle Vulkan validation layers in this backend.
target_compile_definitions(strata_gfx_backend_vk PRIVATE
  STRATA_VK_VALIDATION=$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>
)

# Headers use #include "gfx/rhi/gpu_device.h" and platform types.
target_include_directories(strata_gfx_backend_vk
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/gfx/include # "<strata/gfx/rhi/...>
        ${CMAKE_CURRENT_SOURCE_DIR}            # vk_*.h in this directory
)

target_link_libraries(strata_gfx_backend_vk
    PUBLIC
        engine_options
        Vulkan::Vulkan
        strata_platform       # for WsiHandle, etc.
)

enable_strict_warnings(strata_gfx_backend_vk)

# Vulkan WSI macros localized here
if (WIN32)
  target_compile_definitions(strata_gfx_backend_vk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif (UNIX AND NOT APPLE)
  if (STRATA_USE_X11)
    target_compile_definitions(strata_gfx_backend_vk PRIVATE VK_USE_PLATFORM_XLIB_KHR)
  else()
    target_compile_definitions(strata_gfx_backend_vk PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
  endif()
endif()

# Optional: shader compilation step, adapted from old strata_gfx target
file(GLOB_RECURSE STRATA_GLSL "${CMAKE_CURRENT_SOURCE_DIR}/../../shaders/*.*")
find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
if (GLSLC AND STRATA_GLSL)
  # All compiled shaders go to a common engine/gfx/shaders directory in the build tree
  set(STRATA_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/engine/gfx/shaders")

  set(SPV_OUT "")
  foreach(GLSL ${STRATA_GLSL})
    get_filename_component(NAME ${GLSL} NAME)
    set(SPV "${STRATA_SHADER_OUT_DIR}/${NAME}.spv")
    add_custom_command(
      OUTPUT ${SPV}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${STRATA_SHADER_OUT_DIR}"
      COMMAND ${GLSLC} -std=450 -o ${SPV} ${GLSL}
      DEPENDS ${GLSL}
      COMMENT "Compiling shader ${NAME}"
      VERBATIM
    )
    list(APPEND SPV_OUT ${SPV})
  endforeach()
  add_custom_target(strata_shaders DEPENDS ${SPV_OUT})
  add_dependencies(strata_gfx_backend_vk strata_shaders)
endif()
