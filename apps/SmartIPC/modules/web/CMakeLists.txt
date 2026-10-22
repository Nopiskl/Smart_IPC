add_library(nopiskl_web src/auth.cpp src/http_utils.cpp src/server.cpp)
target_include_directories(nopiskl_web PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nopiskl_web PUBLIC nopiskl_common
  PRIVATE nopiskl_control nopiskl_http nopiskl_crypto Threads::Threads)
nopiskl_enable_warnings(nopiskl_web)
