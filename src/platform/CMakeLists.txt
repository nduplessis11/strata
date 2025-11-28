add_library(strata_platform_new STATIC
    window.h window.cpp
    wsi_handle.h
    clock.h clock.cpp
    filesystem.h filesystem.cpp
    input_raw.h input_raw.cpp
    win32/window_win32.cpp
    x11/window_x11.cpp
    wayland/window_wayland.cpp
)

target_include_directories(strata_platform_new PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(strata_platform_new PUBLIC engine_options)
