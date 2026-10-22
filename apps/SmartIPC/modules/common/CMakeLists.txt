add_library(nopiskl_common src/files.cpp src/system.cpp)
target_include_directories(nopiskl_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nopiskl_common PUBLIC nopiskl_json)
nopiskl_enable_warnings(nopiskl_common)
