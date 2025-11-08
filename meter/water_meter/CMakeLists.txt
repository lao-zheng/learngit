
# water_meter/CMakeLists.txt
# 设置源文件
set(WATER_METER_SOURCES
    water.cpp
    water_meter.cpp
    ${PROJECT_SOURCE_DIR}/third_party/hlog/hlog.cpp  
    
)

# 创建可执行文件
add_executable(water_meter ${WATER_METER_SOURCES})

# 包含目录
target_include_directories(water_meter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/common
    ${PROJECT_SOURCE_DIR}/third_party/libmodbus
    ${PROJECT_SOURCE_DIR}/third_party/httplib
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann
    ${PROJECT_SOURCE_DIR}/third_party/spdlog/include
    ${PROJECT_SOURCE_DIR}/third_party/hlog
    ${PROJECT_SOURCE_DIR}/water_meter/src
)


# 链接库
target_link_libraries(water_meter
    common
    #libmodbus
    pthread
)

# 编译选项
target_compile_options(water_meter PRIVATE
    -Wall -g -lm -DDEBUG
)

# 安装目标
#install(TARGETS water_meter RUNTIME DESTINATION bin)
#install(FILES water_meter_config.json DESTINATION etc/water_meter)