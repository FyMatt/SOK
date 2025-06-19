cmake_minimum_required(VERSION 3.10)
project(SOK)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 OpenSSL
find_package(OpenSSL REQUIRED)

# 递归查找 Core 目录下所有源文件
file(GLOB_RECURSE CORE_SOURCES Core/*.cpp)
file(GLOB_RECURSE CORE_HEADERS Core/*.hpp)

add_executable(SOK SOK.cpp ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(SOK PRIVATE Core)
target_link_libraries(SOK PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)

# 拷贝配置和证书文件到构建目录
configure_file(${CMAKE_SOURCE_DIR}/config.yaml ${CMAKE_BINARY_DIR}/config.yaml COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/server.crt ${CMAKE_BINARY_DIR}/server.crt COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/server.key ${CMAKE_BINARY_DIR}/server.key COPYONLY)