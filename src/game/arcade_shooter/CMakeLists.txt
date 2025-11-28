if (NOT STRATA_BUILD_RHI_VK)
    message(STATUS "Skipping arcade_shooter in new layout: Vulkan backend not available")
    return()
endif()

add_executable(strata_arcade_shooter
    arcade_shooter.cpp
)

target_link_libraries(strata_arcade_shooter
    PRIVATE
        strata_platform
        strata_rhi
        strata_rhi_vk
        strata_renderer
)
