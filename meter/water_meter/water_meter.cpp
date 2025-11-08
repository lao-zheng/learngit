/* #include "water_meter.h"
#include <csignal>
#include <filesystem>
#include "hlog.h"

// 构造函数：初始化配置参数，调用默认配置（后续可被TOML配置覆盖）
WaterMeterReader::WaterMeterReader(const Config& config) : config_(config) {
    ctx_ = nullptr;
    current_meter_index_ = 0;

    // 加载默认配置，确保无TOML文件时程序可运行
    setDefaultConfig();    
}

// 析构函数：调用stop()释放所有资源，避免内存泄漏/资源残留
WaterMeterReader::~WaterMeterReader() {
    stop();
}

// 设置默认配置：所有参数的基础值，TOML文件未配置时生效
void WaterMeterReader::setDefaultConfig() {

    // HTTP默认配置
    config_.http_host = "0.0.0.0";
    config_.http_port = 5002;
    config_.http_routes = "/api/collect/v1/waterMeter/totalT/all";
    
    // Modbus默认配置
    config_.rtu_device = "/dev/ttyUSB0";
    config_.rtu_baudrate = 9600;
    config_.rtu_parity = "N";
    config_.rtu_data_bits = 8;
    config_.rtu_stop_bits = 1;
    
    // 水表默认配置
    config_.meter_count = 9;
    config_.meter_addresses = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    
    // 读取默认配置：3秒轮询一次，最多重试5次，2秒响应超时
    config_.read_interval_ms = 3000;
    config_.max_retry_count = 5;
    config_.response_timeout_ms = 2000;
    config_.read_timeout_ms = config_.response_timeout_ms;
    config_.enable_logging = true;

     // 日志默认路径（预留）
    config_.log_filedir = "/userdata/zhangye/water_meter/logs/water_meter_logs.log";
}


// 加载配置文件：从TOML文件读取参数，覆盖默认配置
bool WaterMeterReader::loadConfig(const std::string& config_file) {
    //检查配置文件是否存在
    if (!std::filesystem::exists(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Configuration file not found: {}", config_file); 
        return true;   // 无文件时返回true，后续用默认配置
    }
     
    try {
        // 解析TOML文件
        auto config_toml = toml::parse_file(config_file);

        // 读取基础配置
        auto base = config_toml["base"];
        config_.enable_logging = base["enable_log"].value_or(true);

        // 读取HTTP服务配置
        auto http_server = config_toml["http_server"];
        config_.http_host = http_server["host"].value_or(config_.http_host);
        config_.http_port = http_server["port"].value_or(config_.http_port);
        config_.http_routes = http_server["routes"].value_or(config_.http_routes);

        // 读取Modbus RTU配置
        auto modbus_rtu = config_toml["modbus_rtu"];
        config_.rtu_device = modbus_rtu["device"].value_or(config_.rtu_device);
        config_.rtu_baudrate = modbus_rtu["baudrate"].value_or(config_.rtu_baudrate);
        config_.rtu_parity = modbus_rtu["parity"].value_or(config_.rtu_parity);
        config_.rtu_data_bits = modbus_rtu["data_bits"].value_or(config_.rtu_data_bits);
        config_.rtu_stop_bits = modbus_rtu["stop_bits"].value_or(config_.rtu_stop_bits);

        // 读取水表配置
        auto meter = config_toml["meter"];
        config_.meter_count = meter["count"].value_or(config_.meter_count);

        // 读取水表地址数组，保持配置文件中的顺序
        if (meter["meters_addresses"].is_array()) {
            auto addresses = meter["meters_addresses"].as_array();
            config_.meter_addresses.clear();
            for (auto& addr : *addresses) {
                config_.meter_addresses.push_back(addr.value_or(1));
            }
        } else {
            // 无地址数组时，按count生成1~count的连续地址
            config_.meter_addresses.clear();
            for (int i = 1; i <= config_.meter_count; i++) {
                config_.meter_addresses.push_back(i);
            }
        }

        // 读取数据读取配置 控制读取频率
        auto data = config_toml["data"];
        config_.read_interval_ms = data["read_interval_ms"].value_or(config_.read_interval_ms);
        config_.max_retry_count = data["max_retry_count"].value_or(config_.max_retry_count);
        config_.response_timeout_ms = data["response_timeout_ms"].value_or(config_.response_timeout_ms);
        config_.read_timeout_ms = config_.response_timeout_ms;
        
        // 读取日志配置 (预留)
        auto log_config = config_toml["log"];
        config_.log_filedir = log_config["filedir"].value_or(config_.log_filedir);
        
        LOG_INFO(LOGGER_CONSOLE, "Configuration loaded: {}", config_file);
        return true;
    } catch (const std::exception& e) {
        // 解析失败（如TOML格式错误），输出错误日志并返回true（用默认配置）
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed: {}, using defaults", e.what());
        return true;
    }
}

bool WaterMeterReader::reconnectModbus() {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    //释放旧的Modbus上下文
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    
    // 创建新的Modbus RTU上下文
    char parity = config_.rtu_parity.empty() ? 'N' : config_.rtu_parity[0];
    ctx_ = modbus_new_rtu(config_.rtu_device.c_str(), config_.rtu_baudrate, 
                         parity, config_.rtu_data_bits, config_.rtu_stop_bits);
    if (ctx_ == nullptr) {
        // 上下文创建失败 如串口不存在
        LOG_ERROR(LOGGER_CONSOLE, "Modbus context creation failed during reconnect");
        return false;
    }
    
    // 设置Modbus响应超时
    modbus_set_response_timeout(ctx_, config_.response_timeout_ms / 1000, 
                               (config_.response_timeout_ms % 1000) * 1000);
    
    // 连接串口
    if (modbus_connect(ctx_) == -1) {
        //连接失败（如串口被占用）
        LOG_ERROR(LOGGER_CONSOLE, "Reconnect failed: {} : {}", config_.rtu_device, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Modbus reconnected: {}", config_.rtu_device);
    return true;
}

// 初始化函数：完成Modbus连接、水表列表初始化、HTTP路由设置
bool WaterMeterReader::initialize() {
    // 创建Modbus RTU上下文   
    char parity = config_.rtu_parity.empty() ? 'N' : config_.rtu_parity[0];
    ctx_ = modbus_new_rtu(config_.rtu_device.c_str(), config_.rtu_baudrate, 
                         parity, config_.rtu_data_bits, config_.rtu_stop_bits);
    if (ctx_ == nullptr) {
        // 上下文创建失败（致命错误）
        LOG_ERROR(LOGGER_CONSOLE, "Modbus context creation failed");
        return false;
    }

    // 设置Modbus响应超时
    modbus_set_response_timeout(ctx_, config_.response_timeout_ms / 1000, 
                               (config_.response_timeout_ms % 1000) * 1000);
    
    // 连接串口
    if (modbus_connect(ctx_) == -1) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus connect failed: {} : {}", config_.rtu_device, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    // 严格按照配置文件顺序初始化水表数据
    {
        // 独占锁：meters_
        std::unique_lock<std::shared_mutex> lock(meters_mutex_);
        // 清空旧列表（防止重复初始化）
        meters_.clear();
        // 初始化每块水表：地址、初始用水量0、读取失败、重试0次、当前时间
        for (int addr : config_.meter_addresses) {
            meters_.push_back({addr, 0, false, 0, std::chrono::system_clock::now()});
        }
    }
    // 设置HTTP路由
    setupHttpRoutes();
    
    // 初始化轮询时间和索引（当前时间为首次读取基准）
    last_read_time_ = std::chrono::steady_clock::now();
    current_meter_index_ = 0;
    
    LOG_INFO(LOGGER_CONSOLE, "Initialized: device={}, meters={}", config_.rtu_device, meters_.size());
    return true;
}

// 设置HTTP路由：注册3个核心接口，处理请求并返回JSON响应
void WaterMeterReader::setupHttpRoutes() {
    // 获取所有水表数据 - 修改为返回浮点数（m³单位）
    http_server_.Get(config_.http_routes, [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json response;
        // 存储所有水表的用水量数组
        nlohmann::json water_values = nlohmann::json::array();
        
        // 使用读锁保护，按照配置文件顺序返回水表数据  共享锁读取meters_时允许其他线程同时读
        {
            std::shared_lock<std::shared_mutex> lock(meters_mutex_);
            for (const auto& meter : meters_) {
                if (meter.success) {
                    // 将0.01m³单位转换为m³单位，保持与日志输出一致
                    double water_m3 = meter.total_water * 0.01;
                    water_values.push_back(water_m3);
                } else {
                     // 读取失败返回-1
                    water_values.push_back(-1); 
                }
            }
        }
        
        response["message"] = water_values;
        response["result"] = 0;
        response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(response.dump(), "application/json");
    });
    
    // 健康检查 返回程序运行状态、Modbus连接状态、水表数量
    http_server_.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json result;
        result["status"] = "ok";
        
        // 检查Modbus连接状态（通过上下文和socket判断）
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        if (ctx_ == nullptr || modbus_get_socket(ctx_) == -1) {
            result["modbus_status"] = "disconnected";
        } else {
            result["modbus_status"] = "connected";
        }
        
        // 补充水表数量和时间戳
        result["meter_count"] = meters_.size();
        result["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        res.set_content(result.dump(), "application/json");
    });
    
    // 单个水表查询 - 修改为返回浮点数（m³单位） 单块水表实时查询接口：按ID实时读取，返回当前用水量
    http_server_.Get("/api/collect/v1/waterMeter/totalT/:id", [this](const httplib::Request& req, httplib::Response& res) {
        int meter_id;
        // 解析URL中的水表ID（如/api/collect/v1/waterMeter/totalT/1中的1
        try {
            meter_id = std::stoi(req.path_params.at("id"));
        } catch (const std::exception& e) {
            nlohmann::json error_response;
            error_response["message"] = "error: invalid meter id";
            error_response["result"] = -1;
            error_response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            res.set_content(error_response.dump(), "application/json");
            return;
        }
        


                // 构造响应
        nlohmann::json response;
        nlohmann::json water_value = nlohmann::json::array();
        

        // 使用读锁保护缓存数据访问
        std::shared_lock<std::shared_mutex> lock(meters_mutex_);

        // 在缓存中查找水表数据
        auto it = std::find_if(meters_.begin(), meters_.end(), 
                  [meter_id](const WaterMeter& meter) { return meter.id == meter_id; });

         
        if (it != meters_.end()) {
            const WaterMeter& cached_data = *it;
            if (cached_data.success) {
                double water_m3 = cached_data.total_water * 0.01;
                water_value.push_back(water_m3);
            } else {
                water_value.push_back(-1);
            }
            response["result"] = 0;
        } else {
            water_value.push_back(-1);
            response["result"] = -1;
        }

        response["message"] = water_value;
        response["result"] = 0;
        response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(response.dump(), "application/json");
    });
}

// 读取单块水表：核心通信逻辑，包含重试、异常重连，线程安全
WaterMeter WaterMeterReader::readSingleMeter(int slave_id) {
    std::lock_guard<std::mutex> lock(modbus_mutex_);

    // 初始化返回结果：默认读取失败
    WaterMeter result{slave_id, 0, false, 0, std::chrono::system_clock::now()};
    
    // 检查Modbus上下文是否为空（如串口断开），为空则尝试重连
    if (ctx_ == nullptr) {
        LOG_WARN(LOGGER_CONSOLE, "Modbus context null, reconnecting");
        if (!reconnectModbus()) {
            return result;
        }
    }
    
    // 重试机制：最多重试max_retry_count次（提升读取成功率）
    for (int retry = 0; retry <= config_.max_retry_count; retry++) {
        if (modbus_set_slave(ctx_, slave_id) != 0) {
            if (config_.enable_logging) {
                LOG_WARN(LOGGER_CONSOLE, "Set slave failed {}: {}", slave_id, modbus_strerror(errno));
            }
            if (retry < config_.max_retry_count) {
                usleep(100000);
                continue;
            }
            break;
        }
        
         //读取Modbus寄存器（水表用水量存储在0x0000地址，2个16位寄存器→32位数据）
        uint16_t regs[2];
        int rc = modbus_read_registers(ctx_, 0x0000, 2, regs);
        if (rc == -1) {
            if (config_.enable_logging) {
                LOG_WARN(LOGGER_CONSOLE, "Read slave {} failed (retry {}/{}): {}", 
                         slave_id, retry + 1, config_.max_retry_count + 1, modbus_strerror(errno));
            }
            
             // 处理连接断开错误（如串口拔插），尝试重连
            if (errno == ECONNRESET || errno == EBADF) {
                LOG_INFO(LOGGER_CONSOLE, "Modbus connection broken, reconnecting");
                if (!reconnectModbus()) {// 重连失败，退出重试
                    break;
                }
            }
            // 未到最大重试次数，等待100ms后重试
            if (retry < config_.max_retry_count) {
                usleep(100000);
                continue;
            }
            // 重试耗尽，退出循环
            break;
        }
        
        //数据转换（大端序：高位寄存器<<16 | 低位寄存器）
        result.total_water = (regs[0] << 16) | regs[1];
        // 标记读取成功
        result.success = true;
        // 记录实际重试次数
        result.retry_count = retry;
        // 读取成功，退出循环
        break;
    }
    
    return result;
}

// 读取下一块水表：按索引循环更新，主线程轮询调用
void WaterMeterReader::readNextMeter() {
    // 无水表时直接返回（避免索引越界）
    if (meters_.empty()) return;
    
    // 获取当前要读取的水表（按current_meter_index_索引）
    auto& meter = meters_[current_meter_index_];
    // 实时读取该水表数据
    WaterMeter new_data = readSingleMeter(meter.id);
    // 独占锁：更新水表数据时互斥，防止与HTTP读操作冲突
    {
        std::unique_lock<std::shared_mutex> lock(meters_mutex_);
        meter.total_water = new_data.total_water;       // 更新用水量
        meter.success = new_data.success;               // 更新读取状态
        meter.retry_count = new_data.retry_count;       // 更新重试次数
        meter.last_update = new_data.last_update;       // 更新时间戳
    }
    // 日志输出
    if (meter.success) {
        double water_m3 = meter.total_water * 0.01;
        LOG_INFO(LOGGER_CONSOLE, "Meter {}: {:.2f} m³", meter.id, water_m3);
    } else {
        LOG_WARN(LOGGER_CONSOLE, "Meter {}: read failed", meter.id);
    }

    // 更新轮询索引：循环遍历所有水表（取模运算实现循环，如3块表：0→1→2→0）
    current_meter_index_ = (current_meter_index_ + 1) % meters_.size();
}

// 主轮询逻辑：运行在main_thread_中，控制读取间隔
void WaterMeterReader::run() {
    LOG_INFO(LOGGER_CONSOLE, "Water meter reader starting");
    
    // 启动HTTP服务器线程（独立运行，不阻塞主轮询）
    http_thread_ = std::thread([this]() {
        LOG_INFO(LOGGER_CONSOLE, "HTTP server starting on {}:{}", config_.http_host, config_.http_port);
        // 监听HTTP端口（阻塞调用，直到stop()调用http_server_.stop()）
        if (!http_server_.listen(config_.http_host.c_str(), config_.http_port)) {
            LOG_ERROR(LOGGER_CONSOLE, "HTTP server failed on port {}", config_.http_port);
        } else {
            LOG_INFO(LOGGER_CONSOLE, "HTTP server stopped");
        }
    });
    
    // 主轮询循环：running_为false时退出（收到停止信号）
    while (running_) {
        // 计算当前时间与上一次读取的时间差（控制轮询间隔）
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_read_time_);
        
        // 时间差≥配置的读取间隔时，读取下一块水表
        if (elapsed.count() >= config_.read_interval_ms) {
            readNextMeter();
            last_read_time_ = now;
        }
        // 短暂睡眠100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Water meter reader stopped");
}

// 启动服务：创建主线程和HTTP线程，设置运行状态
bool WaterMeterReader::start() {

    // 已在运行中，直接返回false（避免重复启动）
    if (running_) {
        LOG_WARN(LOGGER_CONSOLE, "Service already running");
        return false;
    }

    // 设置运行状态为true
    running_ = true;
    // 创建主线程：执行run()函数（轮询逻辑）
    main_thread_ = std::thread(&WaterMeterReader::run, this);
    LOG_INFO(LOGGER_CONSOLE, "Service started");
    return true;
}

// 停止服务：释放所有资源，优雅退出
void WaterMeterReader::stop() {

    // 已停止，直接返回（避免重复停止）
    if (!running_) {
        return;
    }

    // 设置运行状态为false，终止主轮询循环
    LOG_INFO(LOGGER_CONSOLE, "Stopping service...");
    running_ = false;

    // 停止HTTP服务器（解除http_server_.listen()的阻塞）
    http_server_.stop();
    
    // 回收主线程（等待run()函数执行完毕）
    if (main_thread_.joinable()) {
        main_thread_.join();
    }
    
    // 回收HTTP线程（等待HTTP服务器停止）
    if (http_thread_.joinable()) {
        http_thread_.join();
    }
    
    // 释放Modbus资源（关闭连接、释放上下文）
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Service stopped");
} */


#include "water_meter.h"
#include <csignal>
#include <filesystem>
#include "hlog.h"

// BCD码转换为double值
double WaterMeterReader::bcdToDouble(const uint8_t* data, int integerDigits, int fractionalDigits) {
    double result = 0.0;
    double multiplier = 1.0;
    
    // 计算小数部分的乘数
    for (int i = 0; i < fractionalDigits; i++) {
        multiplier *= 0.1;
    }
    
    // 从高位到低位解析BCD码
    for (int i = 0; i < integerDigits + fractionalDigits; i++) {
        int byteIndex = i / 2;
        int nibbleIndex = i % 2;
        uint8_t value;
        
        if (nibbleIndex == 0) {
            value = (data[byteIndex] >> 4) & 0x0F;  // 高4位
        } else {
            value = data[byteIndex] & 0x0F;         // 低4位
        }
        
        result = result * 10 + value;
    }
    
    return result * multiplier;
}

WaterMeterReader::WaterMeterReader(const Config& config) : config_(config) {
    ctx_ = nullptr;
    current_meter_index_ = 0;
    setDefaultConfig();    
}

WaterMeterReader::~WaterMeterReader() {
    stop();
}

void WaterMeterReader::setDefaultConfig() {
    // HTTP默认配置
    config_.http_host = "0.0.0.0";
    config_.http_port = 5002;
    config_.http_routes = "/api/collect/v1/waterMeter/totalT/all";
    
    // Modbus默认配置
    config_.rtu_device = "/dev/ttyUSB0";
    config_.rtu_baudrate = 9600;
    config_.rtu_parity = "N";
    config_.rtu_data_bits = 8;
    config_.rtu_stop_bits = 1;
    
    // 水表默认配置
    config_.meter_count = 9;
    config_.meter_addresses = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    
    // 读取默认配置
    config_.read_interval_ms = 3000;
    config_.max_retry_count = 5;
    config_.response_timeout_ms = 2000;
    config_.read_timeout_ms = config_.response_timeout_ms;
    config_.enable_logging = true;
    config_.log_filedir = "/userdata/zhangye/water_meter/logs/water_meter_logs.log";
}

bool WaterMeterReader::loadConfig(const std::string& config_file) {
    if (!std::filesystem::exists(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Configuration file not found: {}", config_file); 
        return true;
    }
     
    try {
        auto config_toml = toml::parse_file(config_file);

        auto base = config_toml["base"];
        config_.enable_logging = base["enable_log"].value_or(true);

        auto http_server = config_toml["http_server"];
        config_.http_host = http_server["host"].value_or(config_.http_host);
        config_.http_port = http_server["port"].value_or(config_.http_port);
        config_.http_routes = http_server["routes"].value_or(config_.http_routes);

        auto modbus_rtu = config_toml["modbus_rtu"];
        config_.rtu_device = modbus_rtu["device"].value_or(config_.rtu_device);
        config_.rtu_baudrate = modbus_rtu["baudrate"].value_or(config_.rtu_baudrate);
        config_.rtu_parity = modbus_rtu["parity"].value_or(config_.rtu_parity);
        config_.rtu_data_bits = modbus_rtu["data_bits"].value_or(config_.rtu_data_bits);
        config_.rtu_stop_bits = modbus_rtu["stop_bits"].value_or(config_.rtu_stop_bits);

        auto meter = config_toml["meter"];
        config_.meter_count = meter["count"].value_or(config_.meter_count);

        if (meter["meters_addresses"].is_array()) {
            auto addresses = meter["meters_addresses"].as_array();
            config_.meter_addresses.clear();
            for (auto& addr : *addresses) {
                config_.meter_addresses.push_back(addr.value_or(1));
            }
        } else {
            config_.meter_addresses.clear();
            for (int i = 1; i <= config_.meter_count; i++) {
                config_.meter_addresses.push_back(i);
            }
        }

        auto data = config_toml["data"];
        config_.read_interval_ms = data["read_interval_ms"].value_or(config_.read_interval_ms);
        config_.max_retry_count = data["max_retry_count"].value_or(config_.max_retry_count);
        config_.response_timeout_ms = data["response_timeout_ms"].value_or(config_.response_timeout_ms);
        config_.read_timeout_ms = config_.response_timeout_ms;
        
        auto log_config = config_toml["log"];
        config_.log_filedir = log_config["filedir"].value_or(config_.log_filedir);
        
        LOG_INFO(LOGGER_CONSOLE, "Configuration loaded: {}", config_file);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed: {}, using defaults", e.what());
        return true;
    }
}

bool WaterMeterReader::reconnectModbus() {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    
    char parity = config_.rtu_parity.empty() ? 'N' : config_.rtu_parity[0];
    ctx_ = modbus_new_rtu(config_.rtu_device.c_str(), config_.rtu_baudrate, 
                         parity, config_.rtu_data_bits, config_.rtu_stop_bits);
    if (ctx_ == nullptr) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus context creation failed during reconnect");
        return false;
    }
    
    modbus_set_response_timeout(ctx_, config_.response_timeout_ms / 1000, 
                               (config_.response_timeout_ms % 1000) * 1000);
    
    if (modbus_connect(ctx_) == -1) {
        LOG_ERROR(LOGGER_CONSOLE, "Reconnect failed: {} : {}", config_.rtu_device, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Modbus reconnected: {}", config_.rtu_device);
    return true;
}

bool WaterMeterReader::initialize() {
    char parity = config_.rtu_parity.empty() ? 'N' : config_.rtu_parity[0];
    ctx_ = modbus_new_rtu(config_.rtu_device.c_str(), config_.rtu_baudrate, 
                         parity, config_.rtu_data_bits, config_.rtu_stop_bits);
    if (ctx_ == nullptr) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus context creation failed");
        return false;
    }

    modbus_set_response_timeout(ctx_, config_.response_timeout_ms / 1000, 
                               (config_.response_timeout_ms % 1000) * 1000);
    
    if (modbus_connect(ctx_) == -1) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus connect failed: {} : {}", config_.rtu_device, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    {
        std::unique_lock<std::shared_mutex> lock(meters_mutex_);
        meters_.clear();
        for (int addr : config_.meter_addresses) {
            meters_.push_back({addr, 0.0, false, 0, std::chrono::system_clock::now()});
        }
    }
    
    setupHttpRoutes();
    last_read_time_ = std::chrono::steady_clock::now();
    current_meter_index_ = 0;
    
    LOG_INFO(LOGGER_CONSOLE, "Initialized: device={}, meters={}", config_.rtu_device, meters_.size());
    return true;
}

void WaterMeterReader::setupHttpRoutes() {
    // 获取所有水表数据
    http_server_.Get(config_.http_routes, [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json response;
        nlohmann::json water_values = nlohmann::json::array();
        
        {
            std::shared_lock<std::shared_mutex> lock(meters_mutex_);
            for (const auto& meter : meters_) {
                if (meter.success) {
                    water_values.push_back(meter.total_water);
                } else {
                    water_values.push_back(-1);
                }
            }
        }
        
        response["message"] = water_values;
        response["result"] = 0;
        response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(response.dump(), "application/json");
    });
    
    // 健康检查
    http_server_.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json result;
        result["status"] = "ok";
        
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        if (ctx_ == nullptr || modbus_get_socket(ctx_) == -1) {
            result["modbus_status"] = "disconnected";
        } else {
            result["modbus_status"] = "connected";
        }
        
        result["meter_count"] = meters_.size();
        result["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        res.set_content(result.dump(), "application/json");
    });
    
    // 单个水表查询
    http_server_.Get("/api/collect/v1/waterMeter/totalT/:id", [this](const httplib::Request& req, httplib::Response& res) {
        int meter_id;
        try {
            meter_id = std::stoi(req.path_params.at("id"));
        } catch (const std::exception& e) {
            nlohmann::json error_response;
            error_response["message"] = "error: invalid meter id";
            error_response["result"] = -1;
            error_response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            res.set_content(error_response.dump(), "application/json");
            return;
        }
        
        nlohmann::json response;
        nlohmann::json water_value = nlohmann::json::array();

        std::shared_lock<std::shared_mutex> lock(meters_mutex_);
        auto it = std::find_if(meters_.begin(), meters_.end(), 
                  [meter_id](const WaterMeter& meter) { return meter.id == meter_id; });

        if (it != meters_.end()) {
            const WaterMeter& cached_data = *it;
            if (cached_data.success) {
                water_value.push_back(cached_data.total_water);
            } else {
                water_value.push_back(-1);
            }
            response["result"] = 0;
        } else {
            water_value.push_back(-1);
            response["result"] = -1;
        }

        response["message"] = water_value;
        response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(response.dump(), "application/json");
    });
}

// 核心修改：按照协议文档读取累计水量（BCD码格式）
WaterMeter WaterMeterReader::readSingleMeter(int slave_id) {
    std::lock_guard<std::mutex> lock(modbus_mutex_);

    WaterMeter result{slave_id, 0.0, false, 0, std::chrono::system_clock::now()};
    
    if (ctx_ == nullptr) {
        LOG_WARN(LOGGER_CONSOLE, "Modbus context null, reconnecting");
        if (!reconnectModbus()) {
            return result;
        }
    }
    
    for (int retry = 0; retry <= config_.max_retry_count; retry++) {
        if (modbus_set_slave(ctx_, slave_id) != 0) {
            if (config_.enable_logging) {
                LOG_WARN(LOGGER_CONSOLE, "Set slave failed {}: {}", slave_id, modbus_strerror(errno));
            }
            if (retry < config_.max_retry_count) {
                usleep(100000);
                continue;
            }
            break;
        }
        
        // 读取2个寄存器（累计水量）
        uint16_t regs[2];
        int rc = modbus_read_registers(ctx_, 0x0000, 2, regs);
        if (rc == -1) {
            if (config_.enable_logging) {
                LOG_WARN(LOGGER_CONSOLE, "Read slave {} failed (retry {}/{}): {}", 
                         slave_id, retry + 1, config_.max_retry_count + 1, modbus_strerror(errno));
            }
            
            if (errno == ECONNRESET || errno == EBADF) {
                LOG_INFO(LOGGER_CONSOLE, "Modbus connection broken, reconnecting");
                if (!reconnectModbus()) {
                    break;
                }
            }
            
            if (retry < config_.max_retry_count) {
                usleep(100000);
                continue;
            }
            break;
        }
        
        // 将寄存器数据转换为字节数组用于BCD解析
        uint8_t data[4];
        data[0] = (regs[0] >> 8) & 0xFF;  // 寄存器0高字节
        data[1] = regs[0] & 0xFF;         // 寄存器0低字节
        data[2] = (regs[1] >> 8) & 0xFF;  // 寄存器1高字节
        data[3] = regs[1] & 0xFF;         // 寄存器1低字节
        
        // 解析BCD码：4位整数 + 4位小数，范围0000.0000-9999.9999 m³
        result.total_water = bcdToDouble(data, 4, 4);
        result.success = true;
        result.retry_count = retry;
        break;
    }
    
    return result;
}

void WaterMeterReader::readNextMeter() {
    if (meters_.empty()) return;
    
    auto& meter = meters_[current_meter_index_];
    WaterMeter new_data = readSingleMeter(meter.id);
    
    {
        std::unique_lock<std::shared_mutex> lock(meters_mutex_);
        meter.total_water = new_data.total_water;
        meter.success = new_data.success;
        meter.retry_count = new_data.retry_count;
        meter.last_update = new_data.last_update;
    }
    
    if (meter.success) {
        LOG_INFO(LOGGER_CONSOLE, "Meter {}: {:.4f} m³", meter.id, meter.total_water);
    } else {
        LOG_WARN(LOGGER_CONSOLE, "Meter {}: read failed", meter.id);
    }

    current_meter_index_ = (current_meter_index_ + 1) % meters_.size();
}

void WaterMeterReader::run() {
    LOG_INFO(LOGGER_CONSOLE, "Water meter reader starting");
    
    http_thread_ = std::thread([this]() {
        LOG_INFO(LOGGER_CONSOLE, "HTTP server starting on {}:{}", config_.http_host, config_.http_port);
        if (!http_server_.listen(config_.http_host.c_str(), config_.http_port)) {
            LOG_ERROR(LOGGER_CONSOLE, "HTTP server failed on port {}", config_.http_port);
        } else {
            LOG_INFO(LOGGER_CONSOLE, "HTTP server stopped");
        }
    });
    
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_read_time_);
        
        if (elapsed.count() >= config_.read_interval_ms) {
            readNextMeter();
            last_read_time_ = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Water meter reader stopped");
}

bool WaterMeterReader::start() {
    if (running_) {
        LOG_WARN(LOGGER_CONSOLE, "Service already running");
        return false;
    }

    running_ = true;
    main_thread_ = std::thread(&WaterMeterReader::run, this);
    LOG_INFO(LOGGER_CONSOLE, "Service started");
    return true;
}

void WaterMeterReader::stop() {
    if (!running_) {
        return;
    }

    LOG_INFO(LOGGER_CONSOLE, "Stopping service...");
    running_ = false;

    http_server_.stop();
    
    if (main_thread_.joinable()) {
        main_thread_.join();
    }
    
    if (http_thread_.joinable()) {
        http_thread_.join();
    }
    
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Service stopped");
}
