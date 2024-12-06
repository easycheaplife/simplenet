cmake_minimum_required(VERSION 3.10)
project(tcp_server)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 收集核心源文件（排除 main 函数）
file(GLOB CORE_SOURCES 
    "${PROJECT_SOURCE_DIR}/src/*.cc"
)
list(REMOVE_ITEM CORE_SOURCES "${PROJECT_SOURCE_DIR}/src/server.cc")

# 主服务器可执行文件
add_executable(server
    ${CORE_SOURCES}
    ${PROJECT_SOURCE_DIR}/src/server.cc
)

# echo 服务器可执行文件
add_executable(echo_server
    ${CORE_SOURCES}
    ${PROJECT_SOURCE_DIR}/test/echo_server.cc
)

# 测试客户端可执行文件
add_executable(client_test
    ${PROJECT_SOURCE_DIR}/test/client_test.cc
)

# 压力测试可执行文件
add_executable(stress_test
    ${PROJECT_SOURCE_DIR}/test/stress_test.cc
)

# 查找并链接线程库
find_package(Threads REQUIRED)

# 为所有目标链接线程库
foreach(target server echo_server client_test stress_test)
    target_link_libraries(${target} PRIVATE Threads::Threads)
endforeach()

# 创建必要的目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs)

# 安装配置
install(TARGETS server echo_server client_test stress_test
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION include/tcp_server
    FILES_MATCHING PATTERN "*.h"
)

# 添加单元测试支持
enable_testing()

# 自定义目标：清理日志
add_custom_target(clean_logs
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/logs
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/logs
)

# 配置信息输出
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS}")
