# Static analysis placeholder.
#
# jetio_enable_static_analysis(<target>) is called for every project-owned
# executable. Only clang-tidy is wired up for now; add cppcheck, include-what-you-use,
# MSVC /analyze, etc. here.

function(jetio_enable_static_analysis target)
    if(JETIO_ENABLE_CLANG_TIDY)
        find_program(JETIO_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
        set_target_properties(${target} PROPERTIES
            CXX_CLANG_TIDY "${JETIO_CLANG_TIDY_EXE}")
    endif()
endfunction()
