if (NOT STRATA_BUILD_RHI_VK)
    message(STATUS "Skipping arcade_shooter in new layout: Vulkan backend not available")
    return()
endif()

add_executable(strata_arcade_shooter
    arcade_shooter.cpp
)

target_include_directories(strata_arcade_shooter
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(strata_arcade_shooter
    PRIVATE
        strata_platform_new
        strata_rhi
        strata_rhi_vk
        strata_renderer
)
