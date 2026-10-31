add_executable(jetio_info info.cpp)
target_link_libraries(jetio_info PRIVATE jetio::jetio jetio::dev_options)
jetio_enable_static_analysis(jetio_info)
set_target_properties(jetio_info PROPERTIES OUTPUT_NAME jetio-info FOLDER tools)
