add_library(strata_core STATIC
  include/strata/core/application.h
  include/strata/core/action_map.h
  include/strata/core/ecs_min.h
  src/application.cc
  src/action_map.cc
  src/ecs_min.cc
)

target_include_directories(strata_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(strata_core PUBLIC cxx_std_23)
enable_strict_warnings(strata_core)
target_link_libraries(strata_core PUBLIC strata_gfx)
