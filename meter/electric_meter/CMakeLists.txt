
# electric_meter/CMakeLists.txt
# 设置源文件
set(ELECTRIC_METER_SOURCES
    electric.cpp
    electric_meter.cpp
    ${PROJECT_SOURCE_DIR}/third_party/hlog/hlog.cpp  
    
)

# 创建可执行文件
add_executable(electric_meter ${ELECTRIC_METER_SOURCES})

# 包含目录
target_include_directories(electric_meter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/common
    ${PROJECT_SOURCE_DIR}/third_party/libmodbus
    ${PROJECT_SOURCE_DIR}/third_party/httplib
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann
    ${PROJECT_SOURCE_DIR}/third_party/spdlog/include
    ${PROJECT_SOURCE_DIR}/third_party/hlog
    ${PROJECT_SOURCE_DIR}/electric_meter/src
)


# 链接库
target_link_libraries(electric_meter
    common
    #libmodbus
    pthread
)

# 编译选项
target_compile_options(electric_meter PRIVATE
    -Wall -g -lm -DDEBUG
)

