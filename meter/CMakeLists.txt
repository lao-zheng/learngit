cmake_minimum_required(VERSION 3.16)
project(hcd6817 LANGUAGES C CXX)

# 交叉编译配置
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)    
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# 编译选项
set(CMAKE_C_FLAGS "-Wall -g -lm -DDEBUG")
set(CMAKE_CXX_FLAGS "-Wall -g -lm -DDEBUG")

# 打印环境变量
message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")

# 包含目录
include_directories(
    ${PROJECT_SOURCE_DIR}/third_party/libmodbus
    ${PROJECT_SOURCE_DIR}/third_party/httplib
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann
    ${PROJECT_SOURCE_DIR}/third_party/spdlog/include
    ${PROJECT_SOURCE_DIR}/third_party/hlog
    ${PROJECT_SOURCE_DIR}/third_party/spdlog
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus

)

# 查找libmodbus源文件
file(GLOB LIBMODBUS_SOURCES
    ${PROJECT_SOURCE_DIR}/third_party/libmodbus/*.c
)

# 创建libmodbus静态库
add_library(libmodbus STATIC ${LIBMODBUS_SOURCES})

# 设置libmodbus的包含目录
target_include_directories(libmodbus PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party/libmodbus/

)

# 为libmodbus添加编译选项
target_compile_options(libmodbus PRIVATE
    -Wall -g -lm -DDEBUG
)

# 添加公共模块
add_subdirectory(common)

# 添加各个设备程序
add_subdirectory(water_meter)
add_subdirectory(heat_meter)
add_subdirectory(electric_meter)
#add_subdirectory(inverter)
#add_subdirectory(charging_meter)