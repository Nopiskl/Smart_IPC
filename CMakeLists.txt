cmake_minimum_required(VERSION 3.16)
project(nopiskl_video VERSION 1.0.0 LANGUAGES C CXX)

include(cmake/ProjectOptions.cmake)
find_package(Threads REQUIRED)
include(cmake/Dependencies.cmake)

# SmartIPC 自行组装内部模块；示例和测试复用它声明的库目标。
add_subdirectory(apps/SmartIPC)

if(NOPISKL_HOST_SIMULATOR)
  enable_testing()
  add_subdirectory(tests)
else()
  add_subdirectory(examples)
endif()

include(cmake/Install.cmake)
