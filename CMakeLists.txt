cmake_minimum_required(VERSION 3.27)
project(strata LANGUAGES CXX)

# Global project settings (targets still carry their own flags)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(STRATA_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(STRATA_USE_X11 "Use X11 on Linux (else Wayland)" ON)
option(STRATA_ENABLE_ASAN "Enable AddressSanitizer on supported toolchains" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(warnings)  # provides: enable_strict_warnings(<target>)

# Layered targets (Option B: separate gfx layer)
add_subdirectory(engine/platform)
add_subdirectory(engine/gfx)
add_subdirectory(engine/core)
add_subdirectory(games/arcade_shooter)
