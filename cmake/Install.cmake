if(NOT NOPISKL_HOST_SIMULATOR)
  install(DIRECTORY "${NOPISKL_RKLIB}/" DESTINATION lib
    FILES_MATCHING PATTERN "*.so*" PATTERN "cmake" EXCLUDE)
  # 随包提供匹配的编译器运行库，安装布局与 start.sh 保持一致。
  foreach(runtime libstdc++.so.6 libgcc_s.so.1)
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${runtime}"
      OUTPUT_VARIABLE runtime_path OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(EXISTS "${runtime_path}")
      get_filename_component(runtime_real "${runtime_path}" REALPATH)
      install(FILES "${runtime_real}" DESTINATION lib RENAME "${runtime}")
    endif()
  endforeach()
endif()

install(DIRECTORY models/ DESTINATION models)
install(DIRECTORY apps/SmartIPC/config/ DESTINATION config)
install(DIRECTORY apps/SmartIPC/web/ DESTINATION web)
install(PROGRAMS scripts/start.sh DESTINATION .)
install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION .)
install(FILES third_party/civetweb/LICENSE.md DESTINATION licenses RENAME civetweb-MIT.txt)
install(FILES third_party/monocypher/LICENSE.txt DESTINATION licenses RENAME monocypher.txt)
install(FILES third_party/json/LICENSE.MIT DESTINATION licenses RENAME nlohmann-json-MIT.txt)
install(FILES third_party/opencv/LICENSE third_party/rockchip/APACHE-2.0.txt DESTINATION licenses)
install(DIRECTORY docs/ DESTINATION docs)
