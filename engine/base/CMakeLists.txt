# engine/base/CMakeLists.txt

add_library(strata_base 
  STATIC
    src/diagnostics.cpp
)

target_include_directories(strata_base
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(strata_base PUBLIC engine_options)

enable_strict_warnings(strata_base)