include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_jetio_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/jetio")

install(TARGETS jetio
    EXPORT jetioTargets
    FILE_SET HEADERS)

install(EXPORT jetioTargets
    NAMESPACE jetio::
    DESTINATION "${_jetio_cmake_dir}")

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/jetioConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/jetioConfig.cmake"
    INSTALL_DESTINATION "${_jetio_cmake_dir}")

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/jetioConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)

install(FILES
    "${PROJECT_BINARY_DIR}/jetioConfig.cmake"
    "${PROJECT_BINARY_DIR}/jetioConfigVersion.cmake"
    DESTINATION "${_jetio_cmake_dir}")

unset(_jetio_cmake_dir)
