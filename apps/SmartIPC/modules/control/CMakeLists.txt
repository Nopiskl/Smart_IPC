add_library(nopiskl_control src/control.cpp src/motor.cpp src/config_service.cpp
  src/task_queue.cpp src/snapshot_store.cpp)
target_include_directories(nopiskl_control PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nopiskl_control PUBLIC nopiskl_media_api
  PRIVATE nopiskl_config Threads::Threads)
nopiskl_enable_warnings(nopiskl_control)
