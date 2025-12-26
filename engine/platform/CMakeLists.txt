# engine/platform/CMakeLists.txt

add_library(strata_platform STATIC
  src/clock.cpp
  src/filesystem.cpp
  src/input_raw.cpp
)

target_include_directories(strata_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(strata_platform PUBLIC cxx_std_23)
enable_strict_warnings(strata_platform)

# Inherit /utf-8 and UNICODE/_UNICODE from one place
target_link_libraries(strata_platform
  PUBLIC
    engine_options
  PRIVATE
    strata_base
)

if (WIN32)
  # user32, gdi32 for windowing; winmm if you use timeBeginPeriod for timer granularity
  target_link_libraries(strata_platform PUBLIC user32 gdi32 winmm)
  target_sources(strata_platform PRIVATE src/win32/window_win32.cpp)

elseif (UNIX AND NOT APPLE)

  find_package(Threads REQUIRED)

  if (STRATA_USE_X11)
    target_sources(strata_platform PRIVATE src/x11/window_x11.cpp)

    find_package(X11 REQUIRED)

    # Prefer imported targets if your CMake provides them; otherwise fall back.
    if (TARGET X11::X11)
      target_link_libraries(strata_platform PUBLIC X11::X11)
      if (TARGET X11::Xext)
        target_link_libraries(strata_platform PUBLIC X11::Xext)
      endif()
      else()
        # Mark as SYSTEM to reduce noise when -Werror is on.
        target_include_directories(strata_platform SYSTEM PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(strata_platform PUBLIC ${X11_LIBRARIES})
      endif()
    target_link_libraries(strata_platform PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
  else()
    message(FATAL_ERROR "STRATA_USE_X11=OFF selected, but Vulkan Wayland WSI bridge is not implemented yet.")
  endif()
endif()
