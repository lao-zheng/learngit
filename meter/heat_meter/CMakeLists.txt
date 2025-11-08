# heat_meter/CMakeLists.txt
# 设置源文件
set(HEAT_METER_SOURCES
    heat.cpp
    heat_meter.cpp
    ${PROJECT_SOURCE_DIR}/third_party/hlog/hlog.cpp  

)

# 创建可执行文件
add_executable(heat_meter ${HEAT_METER_SOURCES})

# 包含目录
target_include_directories(heat_meter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/common
    ${PROJECT_SOURCE_DIR}/third_party/libmodbus
    ${PROJECT_SOURCE_DIR}/third_party/httplib
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann
    ${PROJECT_SOURCE_DIR}/third_party/spdlog/include
    ${PROJECT_SOURCE_DIR}/third_party/hlog
    ${PROJECT_SOURCE_DIR}/third_party/spdlog
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus
  
)

# 链接库
target_link_libraries(heat_meter
    common
    #libmodbus
    pthread
)

# 编译选项
target_compile_options(heat_meter PRIVATE
    -Wall -g -lm -DDEBUG
)

# 安装目标
#install(TARGETS heat_meter RUNTIME DESTINATION bin)
#install(FILES heat_meter.json DESTINATION etc/heat_meter)