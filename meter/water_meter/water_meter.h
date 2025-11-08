/* #ifndef _WATER_METER_H_
#define _WATER_METER_H_

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

//#define TOML_FILEDIR "/data/data/app/collect/water_meter/config/water_meter_config.toml"
//#define LOG_FILEDIR  "/data/data/app/collect/water_meter/logs/water_meter_logs.log"

#define TOML_FILEDIR "/app/water_meter_config.toml"
#define LOG_FILEDIR  "/app/log/water_meter_logs.log"

// 水表数据结构
struct WaterMeter {
    int         id;                     // 表地址
    uint32_t    total_water;            // 总用水量（单位：0.01 m³）
    bool        success;                // 本次读取是否成功
    int         retry_count;            // 重试次数
    std::chrono::system_clock::time_point last_update; // 最后更新时间
};

struct Config {
    // http配置
    std::string http_host;              // HTTP服务监听地址
    int         http_port;              // HTTP服务监听端口
    std::string http_routes;            // 批量水表数据查询路由

    // modbus配置
    std::string rtu_device;             // 串口设备
    int         rtu_baudrate;           // 波特率
    std::string rtu_parity;             // 校验位
    int         rtu_data_bits;          // 数据位
    int         rtu_stop_bits;          // 停止位

    // 水表配置
    int         meter_count;            // 水表总数
    std::vector<int> meter_addresses;   // 所有水表的Modbus从站地址列表（按配置文件顺序,http表数据顺序）

    // 数据读取配置
    int         read_timeout_ms;        // 读取超时时间
    int         max_retry_count;        // 读取失败最大重试次数
    int         response_timeout_ms;    // Modbus响应超时时间
    int         read_interval_ms;       // 水表轮询间隔
    bool        enable_logging;         // 是否启用日志输出(预留)
    
    // 日志配置
    std::string log_filedir;            // 日志文件路径(预留)
};

class WaterMeterReader {
private: 
    modbus_t*               ctx_;                               // Modbus上下文
    Config                  config_;                            // 配置信息 存储加载的TOML配置或默认值
    std::vector<WaterMeter> meters_;                            // 水表数据列表 按配置文件地址顺序存储
    std::chrono::steady_clock::time_point last_read_time_;      // 最后读取时间 控制轮询间隔
    size_t                  current_meter_index_;               // 当前读取的水表索引 实现循环读取，从0开始
    httplib::Server         http_server_;                       // HTTP服务器 提供3个核心接口：批量查询、健康检查、单表查询
    std::atomic<bool>       running_{false};                    // 运行状态标志 原子类型，线程安全，避免数据竞争
    std::thread             main_thread_;                       // 主工作线程 执行水表轮询逻辑，调用readNextMeter
    std::thread             http_thread_;                       // HTTP服务器线程 独立运行，监听HTTP请求，不阻塞主轮询
    std::mutex              modbus_mutex_;                      // Modbus操作互斥锁 Modbus库非线程安全，防止多线程同时调用
    mutable std::shared_mutex meters_mutex_;                    // 水表数据读写锁 读共享、写互斥：HTTP读数据用共享锁，主线程写数据用独占锁

public:
    // 构造函数：初始化配置参数，调用默认配置函数
    WaterMeterReader(const Config& config);

    // 析构函数：释放所有资源（停止服务、关闭Modbus、回收线程）
    ~WaterMeterReader();

    // 加载配置文件：从TOML文件解析配置，文件不存在/解析失败则用默认配置
    bool loadConfig(const std::string& config_file);

    // 初始化函数：初始化Modbus、连接串口、初始化水表列表、设置HTTP路由
    bool initialize();

    // 设置HTTP路由：其中有3个接口的处理逻辑
    void setupHttpRoutes();

    // 读取单块水表：实现Modbus读取+重试+异常重连，返回单表数据
    WaterMeter readSingleMeter(int slave_id);

    // 读取下一块水表：按索引循环更新单表数据（主线程轮询调用）
    void readNextMeter();

    // 启动服务：设置运行状态为true，创建主线程和HTTP线程
    bool start();

    // 停止服务：设置运行状态为false，停止HTTP服务，回收线程，释放Modbus资源
    void stop();

    // 主轮询逻辑：运行在main_thread_中，按间隔调用readNextMeter
    void run();

private:
    // 设置默认配置：当配置文件加载失败时，用此基础配置保证程序运行
    void setDefaultConfig();
    // Modbus重连函数：当串口断开时，释放旧上下文并重新创建连接（带互斥锁保护）
    bool reconnectModbus();
};

#endif */


#ifndef _WATER_METER_H_
#define _WATER_METER_H_

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

#define DOCKER  0

#if DOCKER
#define TOML_FILEDIR "/app/water_meter_config.toml"
#define LOG_FILEDIR  "/app/log/water_meter_logs.log"
#else
#define TOML_FILEDIR "/userdata/data/app/collect/water_meter/water_meter_config.toml"
#define LOG_FILEDIR  "/userdata/data/app/collect/water_meter/log/water_meter_logs.log"
#endif




// 水表数据结构
struct WaterMeter {
    int         id;                     // 表地址
    double      total_water;            // 总用水量（单位：m³）
    bool        success;                // 本次读取是否成功
    int         retry_count;            // 重试次数
    std::chrono::system_clock::time_point last_update; // 最后更新时间
};

struct Config {
    // http配置
    std::string http_host;              // HTTP服务监听地址
    int         http_port;              // HTTP服务监听端口
    std::string http_routes;            // 批量水表数据查询路由

    // modbus配置
    std::string rtu_device;             // 串口设备
    int         rtu_baudrate;           // 波特率
    std::string rtu_parity;             // 校验位
    int         rtu_data_bits;          // 数据位
    int         rtu_stop_bits;          // 停止位

    // 水表配置
    int         meter_count;            // 水表总数
    std::vector<int> meter_addresses;   // 所有水表的Modbus从站地址列表

    // 数据读取配置
    int         read_timeout_ms;        // 读取超时时间
    int         max_retry_count;        // 读取失败最大重试次数
    int         response_timeout_ms;    // Modbus响应超时时间
    int         read_interval_ms;       // 水表轮询间隔
    bool        enable_logging;         // 是否启用日志输出
    
    // 日志配置
    std::string log_filedir;            // 日志文件路径
};

class WaterMeterReader {
private: 
    modbus_t*               ctx_;                               // Modbus上下文
    Config                  config_;                            // 配置信息
    std::vector<WaterMeter> meters_;                            // 水表数据列表
    std::chrono::steady_clock::time_point last_read_time_;      // 最后读取时间
    size_t                  current_meter_index_;               // 当前读取的水表索引
    httplib::Server         http_server_;                       // HTTP服务器
    std::atomic<bool>       running_{false};                    // 运行状态标志
    std::thread             main_thread_;                       // 主工作线程
    std::thread             http_thread_;                       // HTTP服务器线程
    std::mutex              modbus_mutex_;                      // Modbus操作互斥锁
    mutable std::shared_mutex meters_mutex_;                    // 水表数据读写锁

public:
    WaterMeterReader(const Config& config);
    ~WaterMeterReader();
    bool loadConfig(const std::string& config_file);
    bool initialize();
    void setupHttpRoutes();
    WaterMeter readSingleMeter(int slave_id);
    void readNextMeter();
    bool start();
    void stop();
    void run();

private:
    void setDefaultConfig();
    bool reconnectModbus();
    // BCD码转换函数
    double bcdToDouble(const uint8_t* data, int integerDigits, int fractionalDigits);
};

#endif