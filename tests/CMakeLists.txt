include(JetioDependencies)
include(JetioTesting)

# Aggregate targets: build one kind of test without the others.
add_custom_target(jetio_unit COMMENT "Build all unit tests")
add_custom_target(jetio_bench COMMENT "Build all micro-benchmarks")
add_custom_target(jetio_fuzz COMMENT "Build all fuzz targets")
set_target_properties(jetio_unit jetio_bench jetio_fuzz PROPERTIES FOLDER tests)

if(JETIO_BUILD_TESTS)
    add_subdirectory(unit)
endif()
if(JETIO_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
if(JETIO_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
