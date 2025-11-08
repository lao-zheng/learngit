# common/CMakeLists.txt
# 设置源文件
set(COMMON_SOURCES
    modbus_base.cpp
    #config_loader.cpp
    #http_server.cpp
    #data_parser.cpp
)

# 创建公共库
add_library(common ${COMMON_SOURCES})

# 包含目录
target_include_directories(common PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party/libmodbus
    ${PROJECT_SOURCE_DIR}/third_party/httplib
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann
    ${PROJECT_SOURCE_DIR}/third_party/hlog
    ${PROJECT_SOURCE_DIR}/third_party
    #${PROJECT_SOURCE_DIR}/third_party/spdlog/include
   
   # 
    #${PROJECT_SOURCE_DIR}/third_party/spdlog

)

# 链接库
target_link_libraries(common
    libmodbus
    pthread
)

# 编译选项
target_compile_options(common PRIVATE
    -Wall -g -lm -DDEBUG
)