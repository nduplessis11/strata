# cmake/clang_tidy.cmake
# ------------------------------------------------------------------
# Optional clang-tidy integration
# Controlled by STRATA_ENABLE_CLANG_TIDY
# ------------------------------------------------------------------

if (STRATA_ENABLE_CLANG_TIDY AND NOT DEFINED CMAKE_CXX_CLANG_TIDY)

    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy.exe)

    if (CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY
            ${CLANG_TIDY_EXE}
            --quiet
            --extra-arg=-Wno-unknown-warning-option
        )
    else()
        message(WARNING
            "STRATA_ENABLE_CLANG_TIDY=ON but clang-tidy not found"
        )
    endif()

endif()
