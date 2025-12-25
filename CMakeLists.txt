# CMakeLists.txt

cmake_minimum_required(VERSION 3.27)
project(strata LANGUAGES CXX)

# Global project settings (targets still carry their own flags)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(STRATA_ENABLE_CLANG_TIDY "Enable clang-tidy analysis" OFF)
option(STRATA_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(STRATA_USE_X11 "Use X11 on Linux (else Wayland)" ON)
option(STRATA_ENABLE_ASAN "Enable AddressSanitizer on supported toolchains" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(warnings)  # provides: enable_strict_warnings(<target>)
include(clang_tidy)

# Shared, target-based options for all engine targets
add_library(engine_options INTERFACE)

if (STRATA_ENABLE_ASAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
  target_compile_options(engine_options INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(engine_options INTERFACE -fsanitize=address)
endif()

# Ensure C++23 is requested by all targets that link this.
target_compile_features(engine_options INTERFACE cxx_std_23)

# Make MSVC interpret sources and execution charset as UTF‑8
if (MSVC)
  target_compile_options(engine_options INTERFACE /utf-8)
endif()

# Use wide-character Win32 APIs everywhere on Windows
if (WIN32)
  target_compile_definitions(engine_options INTERFACE UNICODE _UNICODE)
endif()

# Layered targets (Option B: separate gfx layer)
add_subdirectory(engine/platform)
add_subdirectory(engine/gfx)
add_subdirectory(engine/core)
add_subdirectory(games/arcade_shooter)
