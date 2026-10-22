# 按依赖顺序组装内部模块；各模块维护自己的接口和链接边界。
add_subdirectory(modules/common)
add_subdirectory(modules/config)
add_subdirectory(modules/ai)
add_subdirectory(modules/media)
add_subdirectory(modules/control)
add_subdirectory(modules/web)

add_executable(SmartIPC src/main.cpp src/application.cpp src/options.cpp src/admin_setup.cpp)
target_link_libraries(SmartIPC PRIVATE
  nopiskl_web nopiskl_control nopiskl_media nopiskl_config nopiskl_crypto Threads::Threads)
nopiskl_enable_warnings(SmartIPC)
install(TARGETS SmartIPC RUNTIME DESTINATION . OPTIONAL)
