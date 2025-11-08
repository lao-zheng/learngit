#ifndef _INVERTER_READER_H_
#define _INVERTER_READER_H_

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "modbus.h"
#include "toml.hpp"
#include "httplib.h"
#include "json.hpp"
#include "hlog.h"

// 根据环境定义文件路径
#define DOCKER  0
#if DOCKER
#define TOML_FILEDIR "/app/photovoltaic_inverter_config.toml"
#define LOG_FILEDIR  "/app/log/photovoltaic_inverter_log.log"
#else
#define TOML_FILEDIR "/userdata/data/app/collect/photovoltaic_inverter/photovoltaic_inverter_config.toml"
#define LOG_FILEDIR  "/userdata/data/app/collect/photovoltaic_inverter/log/photovoltaic_inverter_log.log"
#endif




// 逆变器数据结构
struct InverterData {
    int         id;                     // 逆变器地址
    std::string name;                   // 逆变器名称
    double      generation;             // 发电量 (kWh)
    bool        success;                // 本次读取是否成功
    int         retry_count;            // 重试次数
    int         register_addr;          // 寄存器地址
    double      multiplier;             // 数据乘数
    std::chrono::system_clock::time_point last_update; // 最后更新时间
};

struct Config {
    // HTTP配置
    std::string http_host;              // HTTP服务监听地址
    int         http_port;              // HTTP服务监听端口
    std::string http_routes;            // 批量逆变器数据查询路由

    // Modbus配置
    std::string rtu_device;             // 串口设备
    int         rtu_baudrate;           // 波特率
    std::string rtu_parity;             // 校验位
    int         rtu_data_bits;          // 数据位
    int         rtu_stop_bits;          // 停止位

    // 逆变器配置
    int         inverter_count;         // 逆变器总数
    std::vector<InverterData> inverters;// 逆变器配置列表

    // 数据读取配置
    int         read_timeout_ms;        // 读取超时时间
    int         max_retry_count;        // 读取失败最大重试次数
    int         response_timeout_ms;    // Modbus响应超时时间
    int         read_interval_ms;       // 逆变器轮询间隔
    bool        enable_logging;         // 是否启用日志输出
    
    // 日志配置
    std::string log_filedir;            // 日志文件路径
};

class InverterReader {
private: 
    modbus_t*               ctx_;                               // Modbus上下文
    Config                  config_;                            // 配置信息
    std::vector<InverterData> inverters_;                       // 逆变器数据列表
    std::chrono::steady_clock::time_point last_read_time_;      // 最后读取时间
    size_t                  current_inverter_index_;            // 当前读取的逆变器索引
    httplib::Server         http_server_;                       // HTTP服务器
    std::atomic<bool>       running_{false};                    // 运行状态标志
    std::thread             main_thread_;                       // 主工作线程
    std::thread             http_thread_;                       // HTTP服务器线程
    std::mutex              modbus_mutex_;                      // Modbus操作互斥锁
    mutable std::shared_mutex inverters_mutex_;                 // 逆变器数据读写锁

public:
    // 构造函数
    InverterReader(const Config& config);
    
    // 析构函数
    ~InverterReader();

    // 加载配置文件
    bool loadConfig(const std::string& config_file);

    // 初始化函数
    bool initialize();

    // 设置HTTP路由
    void setupHttpRoutes();

    // 读取单个逆变器
    InverterData readSingleInverter(int slave_id, int register_addr, double multiplier);

    // 读取下一个逆变器
    void readNextInverter();

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
    
    // 根据逆变器ID查找配置
    InverterData* findInverterConfig(int slave_id);
};

#endif