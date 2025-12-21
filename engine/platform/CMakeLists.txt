# engine/platform/CMakeLists.txt

add_library(strata_platform STATIC
  src/clock.cpp
  src/filesystem.cpp
  src/input_raw.cpp
  $<$<PLATFORM_ID:Windows>:src/win32/window_win32.cpp>
  $<$<PLATFORM_ID:Linux>:  src/x11/window_x11.cpp>
)

target_include_directories(strata_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(strata_platform PUBLIC cxx_std_23)
enable_strict_warnings(strata_platform)

# Inherit /utf-8 and UNICODE/_UNICODE from one place
target_link_libraries(strata_platform PUBLIC engine_options)

if (WIN32)
  # user32, gdi32 for windowing; winmm if you use timeBeginPeriod for timer granularity
  target_link_libraries(strata_platform PUBLIC user32 gdi32 winmm)
elseif (UNIX AND NOT APPLE)
  if (STRATA_USE_X11)
    find_package(X11 REQUIRED)
    find_package(Threads REQUIRED)
    target_include_directories(strata_platform PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(strata_platform PUBLIC ${X11_LIBRARIES} Threads::Threads dl)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    find_package(Threads REQUIRED)
    target_include_directories(strata_platform PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
    target_link_libraries(strata_platform PUBLIC ${WAYLAND_CLIENT_LIBRARIES} Threads::Threads dl)
    target_sources(strata_platform PRIVATE src/wayland/window_wayland.cpp)  # if/when you add it
  endif()
endif()

# Optional: ASan
if (STRATA_ENABLE_ASAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
  target_compile_options(strata_platform PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(strata_platform PRIVATE -fsanitize=address)
endif()
