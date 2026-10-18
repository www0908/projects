cmake_minimum_required(VERSION 3.10)
project(WebServerDemo CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -pthread")
# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 寻找线程库（服务器多线程必选）
find_package(Threads REQUIRED)

# 生成可执行文件 webserver，包含当前目录下的关键源文件
add_executable(webserver
    epollserver.cpp
    sqlConnPool.cpp
    buffer.cpp
    
)

# 链接多线程库与 MySQL 客户端库
target_link_libraries(webserver PRIVATE
    Threads::Threads
    mysqlclient
    crypto
    ssl
)
add_executable(test_buffer
test_buffer.cpp 
buffer.cpp 
)
target_link_libraries(test_buffer PRIVATE 
Threads::Threads 
)

enable_testing()
add_test(NAME buffer_tests COMMAND test_buffer)
