# 接口目标不包含 Rockchip/OpenCV 依赖，控制层仅链接该目标。
add_library(nopiskl_media_api INTERFACE)
target_include_directories(nopiskl_media_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nopiskl_media_api INTERFACE nopiskl_common)

if(NOPISKL_HOST_SIMULATOR)
  add_library(nopiskl_media src/backends/simulated_media.cpp)
  target_link_libraries(nopiskl_media PRIVATE nopiskl_config)
else()
  add_library(nopiskl_media src/backends/rk_media.cpp)
  target_link_libraries(nopiskl_media PRIVATE nopiskl_ai nopiskl_vendor_media)
endif()
target_link_libraries(nopiskl_media PUBLIC nopiskl_media_api PRIVATE Threads::Threads)
nopiskl_enable_warnings(nopiskl_media)
