/*  #include "electric_meter.h"
#include <csignal>
#include <filesystem>
#include <cmath>  // 添加cmath头文件用于std::round
#include "hlog.h"

// 构造函数
ElectricMeterReader::ElectricMeterReader(const ElectricConfig& config) : config_(config) {
    ctx_ = nullptr;
    setDefaultConfig();
}

// 析构函数
ElectricMeterReader::~ElectricMeterReader() {
    stop();
}

// 设置默认配置
void ElectricMeterReader::setDefaultConfig() {
    // HTTP默认配置
    config_.http_host = "0.0.0.0";
    config_.http_port = 5003;
    config_.http_routes = "/api/collect/v1/electricMeter/power/all";
    
    // Modbus TCP默认配置
    config_.tcp_host = "192.168.1.74";
    config_.tcp_port = 502;
    config_.slave_id = 1;
    
    // 电表默认配置（固定值）
    config_.meter_count = 9;
    config_.start_register = 0;
    config_.register_count = 18;
    
    // 读取默认配置
    config_.read_interval_ms = 10000;
    config_.max_retry_count = 3;
    config_.response_timeout_ms = 5000;
    config_.read_timeout_ms = config_.response_timeout_ms;
    config_.enable_logging = true;

    // 日志默认路径
    config_.log_filedir = "/data/data/app/collect/electric_meter/logs/electric_meter_logs.log";
}

// 加载配置文件
bool ElectricMeterReader::loadConfig(const std::string& config_file) {
    if (!std::filesystem::exists(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Configuration file not found: {}", config_file); 
        return true;
    }
     
    try {
        auto config_toml = toml::parse_file(config_file);

        // 读取基础配置
        auto base = config_toml["base"];
        config_.enable_logging = base["enable_log"].value_or(true);

        // 读取HTTP服务配置
        auto http_server = config_toml["http_server"];
        config_.http_host = http_server["host"].value_or(config_.http_host);
        config_.http_port = http_server["port"].value_or(config_.http_port);
        config_.http_routes = http_server["routes"].value_or(config_.http_routes);

        // 读取Modbus TCP配置
        auto modbus_tcp = config_toml["modbus_tcp"];
        config_.tcp_host = modbus_tcp["host"].value_or(config_.tcp_host);
        config_.tcp_port = modbus_tcp["port"].value_or(config_.tcp_port);
        config_.slave_id = modbus_tcp["slave_id"].value_or(config_.slave_id);

        // 电表配置（固定值）- 删除未使用的meter变量
        config_.meter_count = 9; // 固定值
        config_.start_register = 0; // 固定值
        config_.register_count = 18; // 固定值

        // 读取数据读取配置
        auto data = config_toml["data"];
        config_.read_interval_ms = data["read_interval_ms"].value_or(config_.read_interval_ms);
        config_.max_retry_count = data["max_retry_count"].value_or(config_.max_retry_count);
        config_.response_timeout_ms = data["response_timeout_ms"].value_or(config_.response_timeout_ms);
        config_.read_timeout_ms = config_.response_timeout_ms;
        
        // 读取日志配置
        auto log_config = config_toml["log"];
        config_.log_filedir = log_config["filedir"].value_or(config_.log_filedir);
        
        LOG_INFO(LOGGER_CONSOLE, "Configuration loaded: {}", config_file);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed: {}, using defaults", e.what());
        return true;
    }
}

// Modbus重连
bool ElectricMeterReader::reconnectModbus() {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    
    // 创建新的Modbus TCP上下文
    ctx_ = modbus_new_tcp(config_.tcp_host.c_str(), config_.tcp_port);
    if (ctx_ == nullptr) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus TCP context creation failed during reconnect");
        return false;
    }
    
    // 设置超时
    modbus_set_response_timeout(ctx_, config_.response_timeout_ms / 1000, 
                               (config_.response_timeout_ms % 1000) * 1000);
    
    // 设置从站ID
    modbus_set_slave(ctx_, config_.slave_id);
    
    // 连接服务器
    if (modbus_connect(ctx_) == -1) {
        LOG_ERROR(LOGGER_CONSOLE, "Reconnect failed: {}:{} - {}", config_.tcp_host, config_.tcp_port, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Modbus TCP reconnected: {}:{}", config_.tcp_host, config_.tcp_port);
    return true;
}

// 解析浮点数（大端序）
float ElectricMeterReader::parseFloatBigEndian(const uint16_t* regs) {
    union {
        uint32_t i;
        float f;
    } converter;
    
    converter.i = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
    return converter.f;
}

// 初始化
bool ElectricMeterReader::initialize() {
    // 创建Modbus TCP上下文
    ctx_ = modbus_new_tcp(config_.tcp_host.c_str(), config_.tcp_port);
    if (ctx_ == nullptr) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus TCP context creation failed");
        return false;
    }

    // 设置超时
    modbus_set_response_timeout(ctx_, config_.response_timeout_ms / 1000, 
                               (config_.response_timeout_ms % 1000) * 1000);
    
    // 设置从站ID
    modbus_set_slave(ctx_, config_.slave_id);
    
    // 连接服务器
    if (modbus_connect(ctx_) == -1) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus TCP connect failed: {}:{} - {}", 
                 config_.tcp_host, config_.tcp_port, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    // 初始化电表数据结构
    {
        std::unique_lock<std::shared_mutex> lock(meters_mutex_);
        meters_.clear();
        for (int i = 1; i <= config_.meter_count; i++) {
            meters_.push_back({i, 0.0f, false, 0, std::chrono::system_clock::now(), ""});
        }
    }
    
    // 设置HTTP路由
    setupHttpRoutes();
    
    last_read_time_ = std::chrono::steady_clock::now();
    
    LOG_INFO(LOGGER_CONSOLE, "Initialized: {}:{}, meters={}", 
             config_.tcp_host, config_.tcp_port, config_.meter_count);
    return true;
}

// 设置HTTP路由
void ElectricMeterReader::setupHttpRoutes() {
    // 获取所有电表数据
    http_server_.Get(config_.http_routes, [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json response;
        nlohmann::json power_values = nlohmann::json::array();
        
        {
            std::shared_lock<std::shared_mutex> lock(meters_mutex_);
            for (const auto& meter : meters_) {
                if (meter.success) {
                    // 保留两位小数
                    power_values.push_back(std::round(meter.power_value * 100.0) / 100.0);
                } else {
                    power_values.push_back(-1);
                }
            }
        }
        
        response["message"] = power_values;
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
    
    // 单个电表查询
    http_server_.Get("/api/collect/v1/electricMeter/power/:id", [this](const httplib::Request& req, httplib::Response& res) {
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
        nlohmann::json power_value = nlohmann::json::array();

        std::shared_lock<std::shared_mutex> lock(meters_mutex_);
        auto it = std::find_if(meters_.begin(), meters_.end(), 
                  [meter_id](const ElectricMeter& meter) { return meter.id == meter_id; });

        if (it != meters_.end()) {
            const ElectricMeter& cached_data = *it;
            if (cached_data.success) {
                // 保留两位小数
                power_value.push_back(std::round(cached_data.power_value * 100.0) / 100.0);
            } else {
                power_value.push_back(-1);
            }
            response["result"] = 0;
        } else {
            power_value.push_back(-1);
            response["result"] = -1;
        }

        response["message"] = power_value;
        response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(response.dump(), "application/json");
    });
}

// 读取所有电表数据
bool ElectricMeterReader::readAllMeters() {
    std::lock_guard<std::mutex> lock(modbus_mutex_);

    // 检查Modbus连接
    if (ctx_ == nullptr) {
        LOG_WARN(LOGGER_CONSOLE, "Modbus context null, reconnecting");
        if (!reconnectModbus()) {
            return false;
        }
    }
    
    // 重试机制
    for (int retry = 0; retry <= config_.max_retry_count; retry++) {
        // 一次性读取所有电表数据（18个寄存器）
        uint16_t registers[18];
        int rc = modbus_read_registers(ctx_, config_.start_register, config_.register_count, registers);
        
        if (rc == config_.register_count) {
            // 成功读取，解析数据
            std::vector<ElectricMeter> new_data;
            
            for (int i = 0; i < config_.meter_count; i++) {
                int reg_index = i * 2;
                if (reg_index + 1 < config_.register_count) {
                    float power_value = parseFloatBigEndian(&registers[reg_index]);
                    new_data.push_back({
                        i + 1,
                        power_value,
                        true,
                        retry,
                        std::chrono::system_clock::now(),
                        ""
                    });
                    
                    if (config_.enable_logging) {
                        LOG_INFO(LOGGER_CONSOLE, "Meter {}: {:.2f} kWh", i + 1, power_value);
                    }
                }
            }
            
            // 更新数据
            {
                std::unique_lock<std::shared_mutex> lock_meters(meters_mutex_);
                meters_ = new_data;
            }
            
            return true;
        } else {
            std::string error_msg = modbus_strerror(errno);
            LOG_WARN(LOGGER_CONSOLE, "Read failed (retry {}/{}): {}", 
                     retry + 1, config_.max_retry_count + 1, error_msg);
            
            // 处理连接错误
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
            
            // 重试失败，设置错误状态
            std::vector<ElectricMeter> error_data;
            for (int i = 1; i <= config_.meter_count; i++) {
                error_data.push_back({
                    i,
                    0.0f,
                    false,
                    retry,
                    std::chrono::system_clock::now(),
                    error_msg
                });
            }
            
            {
                std::unique_lock<std::shared_mutex> lock_meters(meters_mutex_);
                meters_ = error_data;
            }
            
            break;
        }
    }
    
    return false;
}

// 主轮询逻辑
void ElectricMeterReader::run() {
    LOG_INFO(LOGGER_CONSOLE, "Electric meter reader starting");
    
    // 启动HTTP服务器
    http_thread_ = std::thread([this]() {
        LOG_INFO(LOGGER_CONSOLE, "HTTP server starting on {}:{}", config_.http_host, config_.http_port);
        if (!http_server_.listen(config_.http_host.c_str(), config_.http_port)) {
            LOG_ERROR(LOGGER_CONSOLE, "HTTP server failed on port {}", config_.http_port);
        } else {
            LOG_INFO(LOGGER_CONSOLE, "HTTP server stopped");
        }
    });
    
    // 主循环
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_read_time_);
        
        if (elapsed.count() >= config_.read_interval_ms) {
            readAllMeters();
            last_read_time_ = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Electric meter reader stopped");
}

// 启动服务
bool ElectricMeterReader::start() {
    if (running_) {
        LOG_WARN(LOGGER_CONSOLE, "Service already running");
        return false;
    }

    running_ = true;
    main_thread_ = std::thread(&ElectricMeterReader::run, this);
    LOG_INFO(LOGGER_CONSOLE, "Service started");
    return true;
}

// 停止服务
void ElectricMeterReader::stop() {
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

 */

#include "electric_meter.h"
#include <filesystem>

// 构造函数
ElectricMeterReader::ElectricMeterReader(const ElectricConfig& config) : config_(config) {
    ctx_ = nullptr;
    setDefaultConfig();
}

// 析构函数
ElectricMeterReader::~ElectricMeterReader() {
    stop();
}

// 设置默认配置
void ElectricMeterReader::setDefaultConfig() {
    // HTTP默认配置
    config_.http_host = "0.0.0.0";
    config_.http_port = 5003;
    config_.http_routes = "/api/collect/v1/electricMeter/power/all";
    
    // Modbus TCP默认配置
    config_.tcp_host = "192.168.1.74";
    config_.tcp_port = 502;
    config_.slave_id = 1;
    
    // 电表默认配置（固定值）
    config_.meter_count = 9;
    config_.start_register = 0;
    config_.register_count = 18;
    
    // 读取默认配置
    config_.read_interval_ms = 10000;
    config_.max_retry_count = 3;
    config_.response_timeout_ms = 3000;
    config_.read_timeout_ms = config_.response_timeout_ms;
    config_.enable_logging = true;
    config_.max_reconnect_count = 10;
}

// 加载配置文件
bool ElectricMeterReader::loadConfig(const std::string& config_file) {
    if (!std::filesystem::exists(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Configuration file not found: {}", config_file); 
        return true;
    }
     
    try {
        auto config_toml = toml::parse_file(config_file);

        // 读取基础配置
        auto base = config_toml["base"];
        config_.enable_logging = base["enable_log"].value_or(true);

        // 读取HTTP服务配置
        auto http_server = config_toml["http_server"];
        config_.http_host = http_server["host"].value_or(config_.http_host);
        config_.http_port = http_server["port"].value_or(config_.http_port);
        config_.http_routes = http_server["routes"].value_or(config_.http_routes);

        // 读取Modbus TCP配置
        auto modbus_tcp = config_toml["modbus_tcp"];
        config_.tcp_host = modbus_tcp["host"].value_or(config_.tcp_host);
        config_.tcp_port = modbus_tcp["port"].value_or(config_.tcp_port);
        config_.slave_id = modbus_tcp["slave_id"].value_or(config_.slave_id);

        // 电表配置（固定值）
        config_.meter_count = 9;
        config_.start_register = 0;
        config_.register_count = 18;

        // 读取数据读取配置
        auto data = config_toml["data"];
        config_.read_interval_ms = data["read_interval_ms"].value_or(config_.read_interval_ms);
        config_.max_retry_count = data["max_retry_count"].value_or(config_.max_retry_count);
        config_.response_timeout_ms = data["response_timeout_ms"].value_or(config_.response_timeout_ms);
        config_.max_reconnect_count = data["max_reconnect_count"].value_or(config_.max_reconnect_count);
        config_.read_timeout_ms = config_.response_timeout_ms;
        
        LOG_INFO(LOGGER_CONSOLE, "Configuration loaded: {}", config_file);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed: {}, using defaults", e.what());
        return true;
    }
}

// 检查是否应该退出程序
bool ElectricMeterReader::shouldExit() {
    if (reconnect_count_ >= config_.max_reconnect_count) {
        LOG_ERROR(LOGGER_CONSOLE, "Max reconnect count ({}) exceeded, exiting program", config_.max_reconnect_count);
        return true;
    }
    return false;
}

// Modbus连接函数
bool ElectricMeterReader::connectModbus() {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    
    // 如果已有连接，先断开
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    
    // 创建新的Modbus TCP上下文
    ctx_ = modbus_new_tcp(config_.tcp_host.c_str(), config_.tcp_port);
    if (ctx_ == nullptr) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus TCP context creation failed");
        return false;
    }
    
    // 设置超时
    modbus_set_response_timeout(ctx_, config_.response_timeout_ms / 1000, 
                               (config_.response_timeout_ms % 1000) * 1000);
    
    // 设置从站ID
    modbus_set_slave(ctx_, config_.slave_id);
    
    // 连接服务器
    if (modbus_connect(ctx_) == -1) {
        LOG_ERROR(LOGGER_CONSOLE, "Connection failed: {}:{} - {}", config_.tcp_host, config_.tcp_port, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Modbus TCP connected: {}:{}", config_.tcp_host, config_.tcp_port);
    return true;
}

// 断开Modbus连接
void ElectricMeterReader::disconnectModbus() {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
        LOG_INFO(LOGGER_CONSOLE, "Modbus TCP disconnected");
    }
}

// 解析浮点数（大端序）
float ElectricMeterReader::parseFloatBigEndian(const uint16_t* regs) {
    union {
        uint32_t i;
        float f;
    } converter;
    
    converter.i = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
    return converter.f;
}

// 执行单次读取操作（包含重试逻辑）
bool ElectricMeterReader::performReadWithRetry(uint16_t* registers) {
    std::lock_guard<std::mutex> lock(modbus_mutex_);
    
    if (!ctx_) {
        return false;
    }
    
    // 重试机制
    for (current_retry_count_ = 0; current_retry_count_ <= config_.max_retry_count; current_retry_count_++) {
        int rc = modbus_read_registers(ctx_, config_.start_register, config_.register_count, registers);
        
        if (rc == config_.register_count) {
            // 读取成功，重置重连计数器
            reconnect_count_ = 0;
            return true;
        } else {
            if (current_retry_count_ < config_.max_retry_count) {
                LOG_WARN(LOGGER_CONSOLE, "Read failed (retry {}/{}): {}, waiting to retry...", 
                         current_retry_count_ + 1, config_.max_retry_count, modbus_strerror(errno));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } else {
                LOG_WARN(LOGGER_CONSOLE, "Read failed after {} retries: {}", config_.max_retry_count, modbus_strerror(errno));
            }
        }
    }
    
    return false;
}

// 初始化
bool ElectricMeterReader::initialize() {
    // 设置HTTP路由
    setupHttpRoutes();
    
    // 初始化电表数据结构
    {
        std::unique_lock<std::shared_mutex> lock(meters_mutex_);
        meters_.clear();
        for (int i = 1; i <= config_.meter_count; i++) {
            meters_.push_back({i, 0.0f, false, std::chrono::system_clock::now()});
        }
    }
    
    last_read_time_ = std::chrono::steady_clock::now();
    
    LOG_INFO(LOGGER_CONSOLE, "Electric meter reader initialized");
    return true;
}

// 设置HTTP路由
void ElectricMeterReader::setupHttpRoutes() {
    // 获取所有电表数据
    http_server_.Get(config_.http_routes, [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json response;
        nlohmann::json power_values = nlohmann::json::array();
        
        {
            std::shared_lock<std::shared_mutex> lock(meters_mutex_);
            for (const auto& meter : meters_) {
                if (meter.success) {
                    // 保留两位小数
                    power_values.push_back(std::round(meter.power_value * 100.0) / 100.0);
                } else {
                    power_values.push_back(-1);
                }
            }
        }
        
        response["message"] = power_values;
        response["result"] = 0;
        response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(response.dump(), "application/json");
    });
}

// 读取所有电表数据
bool ElectricMeterReader::readAllMeters() {
    // 检查退出条件
    if (shouldExit()) {
        running_ = false;
        return false;
    }

    // 检查连接状态，如果需要则建立连接
    bool need_connect = false;
    {
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        need_connect = (ctx_ == nullptr || modbus_get_socket(ctx_) == -1);
    }
    
    if (need_connect) {
        LOG_INFO(LOGGER_CONSOLE, "Establishing Modbus connection...");
        if (!connectModbus()) {
            LOG_ERROR(LOGGER_CONSOLE, "Connection failed, reconnect count: {}/{}", 
                     reconnect_count_ + 1, config_.max_reconnect_count);
            reconnect_count_++;
            return false;
        }
        LOG_INFO(LOGGER_CONSOLE, "Connection established successfully");
    }
    
    // 执行读取操作（包含重试）
    uint16_t registers[18];
    bool read_success = performReadWithRetry(registers);
    
    if (read_success) {
        // 成功读取，解析数据
        std::vector<ElectricMeter> new_data;
        
        for (int i = 0; i < config_.meter_count; i++) {
            int reg_index = i * 2;
            if (reg_index + 1 < config_.register_count) {
                float power_value = parseFloatBigEndian(&registers[reg_index]);
                new_data.push_back({
                    i + 1,
                    power_value,
                    true,
                    std::chrono::system_clock::now()
                });
                
                if (config_.enable_logging) {
                    LOG_INFO(LOGGER_CONSOLE, "Meter {}: {:.2f} kWh", i + 1, power_value);
                }
            }
        }
        
        // 更新数据
        {
            std::unique_lock<std::shared_mutex> lock_meters(meters_mutex_);
            meters_ = new_data;
        }
        
        return true;
    } else {
        // 读取失败，断开连接，下次会重连
        LOG_WARN(LOGGER_CONSOLE, "All retries failed, disconnecting...");
        disconnectModbus();
        
        // 设置错误状态
        std::vector<ElectricMeter> error_data;
        for (int i = 1; i <= config_.meter_count; i++) {
            error_data.push_back({
                i,
                0.0f,
                false,
                std::chrono::system_clock::now()
            });
        }
        
        {
            std::unique_lock<std::shared_mutex> lock_meters(meters_mutex_);
            meters_ = error_data;
        }
        
        return false;
    }
}

// 主轮询逻辑
void ElectricMeterReader::run() {
    LOG_INFO(LOGGER_CONSOLE, "Electric meter reader starting");
    
    // 主循环
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_read_time_);
        
        if (elapsed.count() >= config_.read_interval_ms) {
            readAllMeters();
            last_read_time_ = now;
        }
        
        // 短暂休眠
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Electric meter reader stopped");
}

// 启动服务
bool ElectricMeterReader::start() {
    if (running_) {
        LOG_WARN(LOGGER_CONSOLE, "Service already running");
        return false;
    }

    running_ = true;
    
    // 在单独线程中启动HTTP服务器
    std::thread http_thread([this]() {
        LOG_INFO(LOGGER_CONSOLE, "HTTP server starting on {}:{}", config_.http_host, config_.http_port);
        if (!http_server_.listen(config_.http_host.c_str(), config_.http_port)) {
            LOG_ERROR(LOGGER_CONSOLE, "HTTP server failed on port {}", config_.http_port);
        } else {
            LOG_INFO(LOGGER_CONSOLE, "HTTP server stopped");
        }
    });
    http_thread.detach();
    
    // 在主线程中运行主逻辑
    run();
    
    return true;
}

// 停止服务
void ElectricMeterReader::stop() {
    if (!running_) {
        return;
    }

    LOG_INFO(LOGGER_CONSOLE, "Stopping service...");
    running_ = false;

    // 停止HTTP服务器
    http_server_.stop();
    
    // 断开Modbus连接
    disconnectModbus();
    
    LOG_INFO(LOGGER_CONSOLE, "Service stopped");
}