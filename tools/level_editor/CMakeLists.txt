# tools/level_editor/CMakeLists.txt

add_executable(strata_level_editor
  src/main.cpp
)

target_compile_features(strata_level_editor PRIVATE cxx_std_23)
enable_strict_warnings(strata_level_editor)

target_link_libraries(strata_level_editor
  PRIVATE
    engine_options
    strata_core
)

# --- Copy SPIR-V shaders next to the executable ------------------------
# Engine/gfx builds them into ${CMAKE_BINARY_DIR}/engine/gfx/shaders
# We want them at <exe_dir>/shaders so that "shaders/..." works at runtime.
add_custom_command(TARGET strata_level_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:strata_level_editor>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_BINARY_DIR}/engine/gfx/shaders"
          "$<TARGET_FILE_DIR:strata_level_editor>/shaders"
)
