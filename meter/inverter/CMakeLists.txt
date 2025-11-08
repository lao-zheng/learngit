# inverter/CMakeLists.txt
# 设置源文件
set(INVERTER_SOURCES
    inverter.cpp
    inverter_reader.cpp
    ${PROJECT_SOURCE_DIR}/third_party/hlog/hlog.cpp  
)

# 创建可执行文件
add_executable(inverter ${INVERTER_SOURCES})

# 包含目录
target_include_directories(inverter PRIVATE
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
target_link_libraries(inverter
    common
    #libmodbus
    pthread
)

# 编译选项
target_compile_options(inverter PRIVATE
    -Wall -g -lm -DDEBUG
)

# 安装目标
#install(TARGETS inverter RUNTIME DESTINATION bin)
#install(FILES inverter_config.json DESTINATION etc/inverter)