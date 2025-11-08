/*  #ifndef _ELECTRIC_METER_H_
#define _ELECTRIC_METER_H_

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <cstring>
#include "modbus.h"
#include "toml.hpp"
#include "httplib.h"
#include "json.hpp"
#include "hlog.h"

#define TOML_FILEDIR "/data/data/app/collect/electric_meter/config/electric_meter_config.toml"
#define LOG_FILEDIR  "/data/data/app/collect/electric_meter/logs/electric_meter_logs.log"

// 电表数据结构
struct ElectricMeter {
    int         id;                     // 电表ID
    float       power_value;            // 功率值（kWh）
    bool        success;                // 读取是否成功
    int         retry_count;            // 重试次数
    std::chrono::system_clock::time_point last_update; // 最后更新时间
    std::string error_message;          // 错误信息
};

struct ElectricConfig {
    // HTTP配置
    std::string http_host;              // HTTP服务监听地址
    int         http_port;              // HTTP服务监听端口
    std::string http_routes;            // 批量电表数据查询路由

    // Modbus TCP配置
    std::string tcp_host;               // Modbus TCP服务器地址
    int         tcp_port;               // Modbus TCP端口
    int         slave_id;               // 从站ID

    // 电表配置（固定9个电表，从寄存器地址0开始）
    int         meter_count;            // 电表数量（固定为9）
    int         start_register;         // 起始寄存器地址（固定为0）
    int         register_count;         // 寄存器数量（固定为18，9个电表×2寄存器/电表）

    // 数据读取配置
    int         read_timeout_ms;        // 读取超时时间
    int         max_retry_count;        // 读取失败最大重试次数
    int         response_timeout_ms;    // Modbus响应超时时间
    int         read_interval_ms;       // 读取间隔
    bool        enable_logging;         // 是否启用日志输出

    // 日志配置
    std::string log_filedir;            // 日志文件路径
};

class ElectricMeterReader {
private: 
    modbus_t*               ctx_;                               // Modbus上下文
    ElectricConfig          config_;                            // 配置信息
    std::vector<ElectricMeter> meters_;                         // 电表数据列表
    std::chrono::steady_clock::time_point last_read_time_;      // 最后读取时间
    httplib::Server         http_server_;                       // HTTP服务器
    std::atomic<bool>       running_{false};                    // 运行状态标志
    std::thread             main_thread_;                       // 主工作线程
    std::thread             http_thread_;                       // HTTP服务器线程
    std::mutex              modbus_mutex_;                      // Modbus操作互斥锁
    mutable std::shared_mutex meters_mutex_;                    // 电表数据读写锁

        std::atomic<bool> force_quit{false};  // 强制退出标志
    bool safeModbusRead(uint16_t* registers);  // 安全的Modbus读取



public:
    // 构造函数
    ElectricMeterReader(const ElectricConfig& config);
    
    // 析构函数
    ~ElectricMeterReader();

    // 加载配置文件
    bool loadConfig(const std::string& config_file);

    // 初始化函数
    bool initialize();

    // 设置HTTP路由
    void setupHttpRoutes();

    // 读取所有电表数据
    bool readAllMeters();

    // 启动服务
    bool start();

    // 停止服务
    void stop();

    // 主轮询逻辑
    void run();

private:
    // 设置默认配置
    void setDefaultConfig();
    
    // Modbus重连函数
    bool reconnectModbus();
    
    // 解析浮点数（大端序）
    float parseFloatBigEndian(const uint16_t* regs);
};

#endif 

 */


#ifndef _ELECTRIC_METER_H_
#define _ELECTRIC_METER_H_

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <cstring>
#include <cmath>
#include "modbus.h"
#include "toml.hpp"
#include "httplib.h"
#include "json.hpp"
#include "hlog.h"

#define TOML_FILEDIR "/data/data/app/collect/electric_meter/config/electric_meter_config.toml"
#define LOG_FILEDIR  "/data/data/app/collect/electric_meter/logs/electric_meter_logs.log"

// 电表数据结构
struct ElectricMeter {
    int         id;                     // 电表ID
    float       power_value;            // 功率值（kWh）
    bool        success;                // 读取是否成功
    std::chrono::system_clock::time_point last_update; // 最后更新时间
};

struct ElectricConfig {
    // HTTP配置
    std::string http_host;              // HTTP服务监听地址
    int         http_port;              // HTTP服务监听端口
    std::string http_routes;            // 批量电表数据查询路由

    // Modbus TCP配置
    std::string tcp_host;               // Modbus TCP服务器地址
    int         tcp_port;               // Modbus TCP端口
    int         slave_id;               // 从站ID

    // 电表配置（固定9个电表，从寄存器地址0开始）
    int         meter_count;            // 电表数量（固定为9）
    int         start_register;         // 起始寄存器地址（固定为0）
    int         register_count;         // 寄存器数量（固定为18，9个电表×2寄存器/电表）

    // 数据读取配置
    int         read_timeout_ms;        // 读取超时时间
    int         max_retry_count;        // 单次读取最大重试次数
    int         response_timeout_ms;    // Modbus响应超时时间
    int         read_interval_ms;       // 读取间隔
    bool        enable_logging;         // 是否启用日志输出
    int         max_reconnect_count;    // 最大重连次数，超过则程序退出
};

class ElectricMeterReader {
private: 
    modbus_t*               ctx_;                               // Modbus上下文
    ElectricConfig          config_;                            // 配置信息
    std::vector<ElectricMeter> meters_;                         // 电表数据列表
    std::chrono::steady_clock::time_point last_read_time_;      // 最后读取时间
    httplib::Server         http_server_;                       // HTTP服务器
    std::atomic<bool>       running_{false};                    // 运行状态标志
    std::thread             main_thread_;                       // 主工作线程
    std::mutex              modbus_mutex_;                      // Modbus操作互斥锁
    mutable std::shared_mutex meters_mutex_;                    // 电表数据读写锁
    int                     reconnect_count_{0};                // 重连计数器
    int                     current_retry_count_{0};            // 当前重试次数

public:
    // 构造函数
    ElectricMeterReader(const ElectricConfig& config);
    
    // 析构函数
    ~ElectricMeterReader();

    // 加载配置文件
    bool loadConfig(const std::string& config_file);

    // 初始化函数
    bool initialize();

    // 设置HTTP路由
    void setupHttpRoutes();

    // 读取所有电表数据
    bool readAllMeters();

    // 启动服务
    bool start();

    // 停止服务
    void stop();

    // 主轮询逻辑
    void run();

private:
    // 设置默认配置
    void setDefaultConfig();
    
    // Modbus连接函数
    bool connectModbus();
    
    // 断开Modbus连接
    void disconnectModbus();
    
    // 解析浮点数（大端序）
    float parseFloatBigEndian(const uint16_t* regs);
    
    // 检查是否应该退出程序
    bool shouldExit();
    
    // 执行单次读取操作（包含重试逻辑）
    bool performReadWithRetry(uint16_t* registers);
};

#endif

{
    "message":[表1,2,3,4，5,6,7,8,表9]， //其中有单个表解析错误或其他异常，该表为-1
    "result":1,
    "timestamp":1756910866
}

{
    "message":"tcp Connect error" #错误原因 示例
    "result":0,     
    "timestamp":1756910866
}