add_executable(strata_shooter
  src/main.cc
)

target_compile_features(strata_shooter PRIVATE cxx_std_23)
enable_strict_warnings(strata_shooter)
target_link_libraries(strata_shooter PRIVATE engine_options strata_core)
