# Compile and link options for project-owned targets (tests, benchmarks, fuzzers, tools).
# They live on an INTERFACE target that is never installed, so consumers of the
# library are not affected.

add_library(jetio_dev_options INTERFACE)
add_library(jetio::dev_options ALIAS jetio_dev_options)

set(_gnu_like "$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>")
set(_msvc "$<COMPILE_LANG_AND_ID:CXX,MSVC>")

# Warnings. -pedantic-errors turns every non-ISO extension into an error.
target_compile_options(jetio_dev_options INTERFACE
    "$<${_gnu_like}:-Wall;-Wextra;-Wpedantic;-pedantic-errors;-Wshadow;-Wconversion;-Wsign-conversion;-Wold-style-cast;-Wcast-align;-Wnon-virtual-dtor;-Woverloaded-virtual;-Wdouble-promotion;-Wformat=2;-Wimplicit-fallthrough>"
    "$<${_msvc}:/W4;/permissive-;/Zc:__cplusplus;/utf-8;/EHsc>")

if(JETIO_WARNINGS_AS_ERRORS)
    target_compile_options(jetio_dev_options INTERFACE
        "$<${_gnu_like}:-Werror>"
        "$<${_msvc}:/WX>")
endif()

# Release builds keep debug symbols.
target_compile_options(jetio_dev_options INTERFACE
    "$<$<AND:${_gnu_like},$<CONFIG:Release>>:-g>"
    "$<$<AND:${_msvc},$<CONFIG:Release>>:/Zi>")
target_link_options(jetio_dev_options INTERFACE
    "$<$<AND:${_msvc},$<CONFIG:Release>>:/DEBUG;/OPTIMIZE:REF;/OPTIMIZE:ICF>")

# Standard library assertions in Debug (libstdc++ and libc++; each ignores the other's macro).
target_compile_definitions(jetio_dev_options INTERFACE
    "$<$<AND:${_gnu_like},$<CONFIG:Debug>>:_GLIBCXX_ASSERTIONS>"
    "$<$<AND:${_gnu_like},$<CONFIG:Debug>>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>")

# Sanitizers.
if(JETIO_SANITIZE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "JETIO_SANITIZE is only supported with GCC and Clang")
    endif()
    if("thread" IN_LIST JETIO_SANITIZE AND
       ("address" IN_LIST JETIO_SANITIZE OR "memory" IN_LIST JETIO_SANITIZE))
        message(FATAL_ERROR "The thread sanitizer cannot be combined with address or memory")
    endif()
    list(JOIN JETIO_SANITIZE "," _sanitize_flag)
    target_compile_options(jetio_dev_options INTERFACE
        -fsanitize=${_sanitize_flag} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(jetio_dev_options INTERFACE -fsanitize=${_sanitize_flag})
endif()

unset(_gnu_like)
unset(_msvc)
