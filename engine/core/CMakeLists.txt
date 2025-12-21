# engine/core/CMakeLists.txt

add_library(strata_core STATIC
  src/application.cpp
  src/action_map.cpp
  src/ecs_min.cpp
)

target_include_directories(strata_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(strata_core PUBLIC cxx_std_23)
enable_strict_warnings(strata_core)

target_link_libraries(strata_core 
  PUBLIC 
    engine_options 
    strata_platform
    strata_gfx_renderer
)
