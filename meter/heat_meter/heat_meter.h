#ifndef _HEAT_METER_H_
#define _HEAT_METER_H_

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include "modbus.h"
#include <unistd.h>
#include <cstring>
#include "toml.hpp"
#include "httplib.h"    
#include "json.hpp"
#include "hlog.h"

//#define TOML_FILEDIR "/data/data/app/collect/heat_meter/config/heat_meter_config.toml"
//#define LOG_FILEDIR  "/data/data/app/collect/heat_meter/logs/heat_meter_logs.log"

#define TOML_FILEDIR "/app/heat_meter_config.toml"
#define LOG_FILEDIR  "/app/log/heat_meter_logs.log"

// 热量表数据结构
struct HeatMeterData {
    int address;                                    // 热量表地址
    std::string name;                               // 热量表名称
    uint32_t accumulated_heat;                      // 累积热量（原始值）
    double accumulated_heat_kwh;                    // 累积热量（kWh）
    bool success;                                   // 本次读取是否成功
    int retry_count;                                // 重试次数
    std::chrono::system_clock::time_point last_update; // 最后更新时间
    std::string error_message;                      // 错误信息
    
    // 寄存器配置
    int heat_accumulated_addr;                      // 热量累积寄存器地址
    int heat_accumulated_len;                       // 寄存器长度
    double multiplier;                              // 数据乘数
};

// 应用程序配置
struct HeatMeterConfig {
    // HTTP配置
    std::string http_host;                          // HTTP服务监听地址
    int http_port;                                  // HTTP服务监听端口
    std::string http_routes;                        // 热量表数据查询路由

    // Modbus配置
    std::string device_path;                        // 串口设备路径
    int baudrate;                                   // 波特率
    int timeout_ms;                                 // 超时时间
    int retry_count;                                // 重试次数

    // 热量表配置
    int meter_address;                              // 热量表地址
    std::string meter_name;                         // 热量表名称
    int read_interval_ms;                           // 读取间隔
    bool enable_log;                                // 是否启用日志

    // 寄存器配置
    int heat_accumulated_addr;                      // 热量累积寄存器地址
    int heat_accumulated_len;                       // 寄存器长度
    double multiplier;                              // 数据乘数
    
    // 日志配置
    std::string log_filedir;                        // 日志文件路径
};

class HeatMeterReader {
private:
    modbus_t* ctx_;                                 // Modbus上下文
    HeatMeterConfig config_;                        // 配置信息
    HeatMeterData meter_data_;                      // 热量表数据
    std::mutex data_mutex_;                         // 数据互斥锁
    std::atomic<bool> running_{false};              // 运行状态标志
    std::thread main_thread_;                       // 主工作线程
    std::thread http_thread_;                       // HTTP服务器线程
    httplib::Server http_server_;                   // HTTP服务器

public:
    // 构造函数
    HeatMeterReader(const HeatMeterConfig& config);
    
    // 析构函数
    ~HeatMeterReader();

    // 设置默认配置
    void setDefaultConfig();

    // 加载配置文件
    bool loadConfig(const std::string& config_file);

    // 初始化函数
    bool initialize();

    // 设置HTTP路由
    void setupHttpRoutes();

    // 读取热量表数据
    bool readHeatMeter();

    // 获取当前数据（线程安全）
    HeatMeterData getCurrentData();

    // 启动服务
    bool start();

    // 停止服务
    void stop();

    // 主轮询逻辑
    void run();

private:
    // Modbus重连函数
    bool reconnectModbus();
};

#endif