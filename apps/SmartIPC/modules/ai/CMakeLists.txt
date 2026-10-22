add_library(nopiskl_postprocess src/postprocess.cpp)
target_include_directories(nopiskl_postprocess PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nopiskl_postprocess PUBLIC nopiskl_rknn_headers)
nopiskl_enable_warnings(nopiskl_postprocess)

if(NOT NOPISKL_HOST_SIMULATOR)
  add_library(nopiskl_ai src/yolov5.cpp src/detector.cpp)
  target_include_directories(nopiskl_ai PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(nopiskl_ai PRIVATE
    nopiskl_postprocess nopiskl_vendor_rknn nopiskl_vendor_opencv)
  nopiskl_enable_warnings(nopiskl_ai)
endif()
