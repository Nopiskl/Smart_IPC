add_executable(nopiskl_postprocess_test test_postprocess.cpp)
target_link_libraries(nopiskl_postprocess_test PRIVATE nopiskl_postprocess)
nopiskl_enable_warnings(nopiskl_postprocess_test)
add_test(NAME yolov5_postprocess COMMAND nopiskl_postprocess_test
  "${PROJECT_SOURCE_DIR}/models/yolov5/coco_80_labels_list.txt")

# 仅链接控制层及其接口依赖；不依赖具体媒体后端、HTTP 或 Rockchip 动态库。
add_executable(nopiskl_control_test test_control.cpp)
target_link_libraries(nopiskl_control_test PRIVATE nopiskl_control nopiskl_config Threads::Threads)
nopiskl_enable_warnings(nopiskl_control_test)
add_test(NAME control_backends COMMAND nopiskl_control_test
  "${PROJECT_SOURCE_DIR}/apps/SmartIPC/config/default.json")

# 每个公开头文件独立作为一个翻译单元编译，防止依赖隐式 include 顺序。
set(header_sources)
foreach(module common config ai media control web)
  file(GLOB public_headers CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/apps/SmartIPC/modules/${module}/include/nopiskl/${module}/*.h")
  foreach(header IN LISTS public_headers)
    get_filename_component(name "${header}" NAME)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/headers/${module}_${name}.cpp")
    file(WRITE "${source}" "#include <nopiskl/${module}/${name}>\n")
    list(APPEND header_sources "${source}")
  endforeach()
endforeach()
add_library(nopiskl_public_headers_check OBJECT ${header_sources})
target_link_libraries(nopiskl_public_headers_check PRIVATE
  nopiskl_common nopiskl_config nopiskl_postprocess nopiskl_media_api nopiskl_control nopiskl_web)
nopiskl_enable_warnings(nopiskl_public_headers_check)
