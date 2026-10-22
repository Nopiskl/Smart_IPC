# 供应商路径和编译定义仅在这里封装；业务模块不直接拼接 SDK 库路径。
set(NOPISKL_THIRD_PARTY_DIR "${PROJECT_SOURCE_DIR}/third_party")
set(NOPISKL_RKROOT "${NOPISKL_THIRD_PARTY_DIR}/rockchip")
set(NOPISKL_RKLIB "${NOPISKL_RKROOT}/lib/${NOPISKL_LIBC}")

add_library(nopiskl_json INTERFACE)
target_include_directories(nopiskl_json SYSTEM INTERFACE "${NOPISKL_THIRD_PARTY_DIR}/json")

add_library(nopiskl_crypto STATIC "${NOPISKL_THIRD_PARTY_DIR}/monocypher/monocypher.c")
target_include_directories(nopiskl_crypto SYSTEM PUBLIC "${NOPISKL_THIRD_PARTY_DIR}/monocypher")
add_library(nopiskl_http STATIC "${NOPISKL_THIRD_PARTY_DIR}/civetweb/src/civetweb.c")
target_include_directories(nopiskl_http SYSTEM PUBLIC "${NOPISKL_THIRD_PARTY_DIR}/civetweb/include")
target_compile_definitions(nopiskl_http PRIVATE NO_SSL NO_CGI NO_FILES NO_CACHING)
target_link_libraries(nopiskl_http PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# 后处理可在主机运行，只需要张量声明，不链接板端 RKNN 运行库。
add_library(nopiskl_rknn_headers INTERFACE)
target_include_directories(nopiskl_rknn_headers SYSTEM INTERFACE "${NOPISKL_RKROOT}/include/rknn")
target_compile_definitions(nopiskl_rknn_headers INTERFACE RV1106_1103)

if(NOPISKL_HOST_SIMULATOR)
  return()
endif()
if(NOT CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "ARM libraries require a toolchain. Use ./build.sh, or enable NOPISKL_HOST_SIMULATOR.")
endif()

add_library(nopiskl_vendor_rknn INTERFACE)
target_link_libraries(nopiskl_vendor_rknn INTERFACE
  nopiskl_rknn_headers "${NOPISKL_RKLIB}/librknnmrt.so")

add_library(nopiskl_vendor_opencv INTERFACE)
target_include_directories(nopiskl_vendor_opencv SYSTEM INTERFACE
  "${NOPISKL_THIRD_PARTY_DIR}/opencv/include")
foreach(name opencv_highgui opencv_imgproc opencv_core)
  target_link_libraries(nopiskl_vendor_opencv INTERFACE "${NOPISKL_RKLIB}/lib/lib${name}.a")
endforeach()

add_library(nopiskl_vendor_media INTERFACE)
target_include_directories(nopiskl_vendor_media SYSTEM INTERFACE
  "${NOPISKL_RKROOT}/include" "${NOPISKL_RKROOT}/include/rkaiq"
  "${NOPISKL_RKROOT}/include/rkaiq/uAPI2" "${NOPISKL_RKROOT}/include/rkaiq/common"
  "${NOPISKL_RKROOT}/include/rkaiq/xcore" "${NOPISKL_RKROOT}/include/rkaiq/algos"
  "${NOPISKL_RKROOT}/include/rkaiq/iq_parser" "${NOPISKL_RKROOT}/include/rkaiq/iq_parser_v2"
  "${NOPISKL_RKROOT}/include/rkaiq/smartIr" "${NOPISKL_THIRD_PARTY_DIR}")
target_compile_definitions(nopiskl_vendor_media INTERFACE
  ISP_HW_V30 RKPLATFORM UAPI2 _FILE_OFFSET_BITS=64)
target_link_libraries(nopiskl_vendor_media INTERFACE nopiskl_vendor_opencv)
foreach(name rtsp rockit rockchip_mpp rkaiq rga)
  # 不使用跨 profile 的 find_library 缓存，避免切换 libc 后误连旧库。
  unset(NOPISKL_${name} CACHE)
  find_library(NOPISKL_${name} NAMES ${name} PATHS "${NOPISKL_RKLIB}" NO_DEFAULT_PATH)
  if(NOT NOPISKL_${name})
    message(FATAL_ERROR "Missing vendor library ${name} for ${NOPISKL_LIBC}")
  endif()
  target_link_libraries(nopiskl_vendor_media INTERFACE "${NOPISKL_${name}}")
endforeach()
if(NOPISKL_LIBC STREQUAL "uclibc")
  target_link_libraries(nopiskl_vendor_media INTERFACE "${NOPISKL_RKLIB}/librockiva.so")
elseif(NOPISKL_LIBC STREQUAL "glibc")
  target_link_libraries(nopiskl_vendor_media INTERFACE "${NOPISKL_RKLIB}/libfunc_none.so")
else()
  message(FATAL_ERROR "Unknown libc: ${NOPISKL_LIBC}")
endif()
target_link_libraries(nopiskl_vendor_media INTERFACE dl m rt)
target_link_options(nopiskl_vendor_media INTERFACE "-Wl,-rpath-link,${NOPISKL_RKLIB}")
