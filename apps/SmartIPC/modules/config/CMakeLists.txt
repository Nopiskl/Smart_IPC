add_library(nopiskl_config src/config.cpp)
target_include_directories(nopiskl_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nopiskl_config PUBLIC nopiskl_common)
nopiskl_enable_warnings(nopiskl_config)
