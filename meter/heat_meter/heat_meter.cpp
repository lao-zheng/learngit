#include "heat_meter.h"
#include <filesystem>
#include <cmath>

// 构造函数
HeatMeterReader::HeatMeterReader(const HeatMeterConfig& config) :  ctx_(nullptr),config_(config) {}

// 析构函数
HeatMeterReader::~HeatMeterReader() {
    stop();
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
}

// 设置默认配置
void HeatMeterReader::setDefaultConfig() {
    // HTTP默认配置
    config_.http_host = "0.0.0.0";
    config_.http_port = 5003;
    config_.http_routes = "/api/collect/v1/heatMeter/all";
    
    // Modbus默认配置
    config_.device_path = "/dev/ttysWK3";
    config_.baudrate = 9600;
    config_.timeout_ms = 2000;
    config_.retry_count = 3;
    
    // 热量表配置
    config_.meter_address = 24;
    config_.meter_name = "Heat_Meter_24";
    config_.read_interval_ms = 5000;
    config_.enable_log = true;
    
    // 寄存器配置
    config_.heat_accumulated_addr = 10;
    config_.heat_accumulated_len = 2;
    config_.multiplier = 1.0;
    
    // 日志配置
    config_.log_filedir = LOG_FILEDIR;
}

// 加载配置文件
bool HeatMeterReader::loadConfig(const std::string& config_file) {
    if (!std::filesystem::exists(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Configuration file not found: {}", config_file);
        return true; // 使用默认配置
    }
    
    try {
        auto config_toml = toml::parse_file(config_file);

        // 读取基础配置
        auto base = config_toml["base"];
        config_.enable_log = base["enable_log"].value_or(true);

        // 读取HTTP服务配置
        auto http_server = config_toml["http_server"];
        config_.http_host = http_server["host"].value_or(config_.http_host);
        config_.http_port = http_server["port"].value_or(config_.http_port);
        config_.http_routes = http_server["routes"].value_or(config_.http_routes);

        // 读取Modbus配置
        auto modbus = config_toml["modbus"];
        config_.device_path = modbus["device"].value_or(config_.device_path);
        config_.baudrate = modbus["baudrate"].value_or(config_.baudrate);
        config_.timeout_ms = modbus["timeout_ms"].value_or(config_.timeout_ms);
        config_.retry_count = modbus["retry_count"].value_or(config_.retry_count);

        // 读取热量表配置
        auto heat_meter = config_toml["heat_meter"];
        config_.meter_address = heat_meter["address"].value_or(config_.meter_address);
        config_.meter_name = heat_meter["name"].value_or(config_.meter_name);
        config_.read_interval_ms = heat_meter["read_interval_ms"].value_or(config_.read_interval_ms);
        config_.enable_log = heat_meter["enable_log"].value_or(config_.enable_log);
        
        // 读取寄存器配置
        auto registers = config_toml["registers"];
        config_.heat_accumulated_addr = registers["heat_accumulated_addr"].value_or(config_.heat_accumulated_addr);
        config_.heat_accumulated_len = registers["heat_accumulated_len"].value_or(config_.heat_accumulated_len);
        config_.multiplier = registers["multiplier"].value_or(config_.multiplier);
        
        LOG_INFO(LOGGER_CONSOLE, "Heat meter configuration loaded: {}", config_file);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed: {}, using defaults", e.what());
        return true;
    }
}

// 初始化函数
bool HeatMeterReader::initialize() {
    // 创建Modbus RTU上下文
    ctx_ = modbus_new_rtu(config_.device_path.c_str(), config_.baudrate, 'N', 8, 1);
    if (ctx_ == nullptr) {
        LOG_ERROR(LOGGER_CONSOLE, "Unable to create modbus context");
        return false;
    }
    
    // 设置超时
    modbus_set_response_timeout(ctx_, config_.timeout_ms / 1000, 
                               (config_.timeout_ms % 1000) * 1000);
    
    // 连接串口
    if (modbus_connect(ctx_) == -1) {
        LOG_ERROR(LOGGER_CONSOLE, "Connection failed: {} : {}", config_.device_path, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    // 初始化热量表数据
    meter_data_ = {
        config_.meter_address,
        config_.meter_name,
        0,
        0.0,
        false,
        0,
        std::chrono::system_clock::now(),
        "",
        config_.heat_accumulated_addr,
        config_.heat_accumulated_len,
        config_.multiplier
    };
    
    // 设置HTTP路由
    setupHttpRoutes();
    
    LOG_INFO(LOGGER_CONSOLE, "Heat meter reader initialized: device={}, address={}", 
             config_.device_path, config_.meter_address);
    
    return true;
}

// 设置HTTP路由
void HeatMeterReader::setupHttpRoutes() {
    // 获取热量表数据
    http_server_.Get(config_.http_routes, [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json response;
        nlohmann::json heat_values = nlohmann::json::array();
        
        HeatMeterData data = getCurrentData();
        if (data.success) {
            // 保留两位小数
            double rounded_value = std::round(data.accumulated_heat_kwh * 100.0) / 100.0;
            heat_values.push_back(rounded_value);
        } else {
            heat_values.push_back(-1);
        }
        
        response["message"] = heat_values;
        response["result"] = data.success ? 0 : -1;
        response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(response.dump(), "application/json");
    });
    
    // 健康检查
    http_server_.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json result;
        result["status"] = "ok";
        
        if (ctx_ == nullptr || modbus_get_socket(ctx_) == -1) {
            result["modbus_status"] = "disconnected";
        } else {
            result["modbus_status"] = "connected";
        }
        
        HeatMeterData data = getCurrentData();
        result["heat_meter_status"] = data.success ? "connected" : "disconnected";
        result["last_update"] = std::chrono::system_clock::to_time_t(data.last_update);
        // 保留两位小数
        result["accumulated_heat_kwh"] = std::round(data.accumulated_heat_kwh * 100.0) / 100.0;
        result["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(result.dump(), "application/json");
    });
    
    // 详细信息查询
    http_server_.Get("/api/collect/v1/heatMeter/detail", [this](const httplib::Request& req, httplib::Response& res) {
        HeatMeterData data = getCurrentData();
        nlohmann::json result;
        
        result["address"] = data.address;
        result["name"] = data.name;
        result["accumulated_heat_raw"] = data.accumulated_heat;
        // 保留两位小数
        result["accumulated_heat_kwh"] = std::round(data.accumulated_heat_kwh * 100.0) / 100.0;
        result["success"] = data.success;
        result["retry_count"] = data.retry_count;
        result["last_update"] = std::chrono::system_clock::to_time_t(data.last_update);
        result["multiplier"] = data.multiplier;
        
        if (!data.success && !data.error_message.empty()) {
            result["error_message"] = data.error_message;
        }
        
        res.set_content(result.dump(), "application/json");
    });
}

// Modbus重连
bool HeatMeterReader::reconnectModbus() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    
    ctx_ = modbus_new_rtu(config_.device_path.c_str(), config_.baudrate, 'N', 8, 1);
    if (ctx_ == nullptr) {
        LOG_ERROR(LOGGER_CONSOLE, "Modbus context creation failed during reconnect");
        return false;
    }
    
    modbus_set_response_timeout(ctx_, config_.timeout_ms / 1000, 
                               (config_.timeout_ms % 1000) * 1000);
    
    if (modbus_connect(ctx_) == -1) {
        LOG_ERROR(LOGGER_CONSOLE, "Reconnect failed: {} : {}", config_.device_path, modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Modbus reconnected: {}", config_.device_path);
    return true;
}

// 读取热量表数据
bool HeatMeterReader::readHeatMeter() {
    HeatMeterData new_data = {
        config_.meter_address,
        config_.meter_name,
        0,
        0.0,
        false,
        0,
        std::chrono::system_clock::now(),
        "",
        config_.heat_accumulated_addr,
        config_.heat_accumulated_len,
        config_.multiplier
    };
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    if (ctx_ == nullptr) {
        LOG_WARN(LOGGER_CONSOLE, "Modbus context null, reconnecting");
        if (!reconnectModbus()) {
            meter_data_ = new_data;
            return false;
        }
    }
    
    for (int retry = 0; retry <= config_.retry_count; retry++) {
        // 设置从机地址
        if (modbus_set_slave(ctx_, config_.meter_address) != 0) {
            new_data.error_message = "Failed to set slave address: " + std::string(modbus_strerror(errno));
            if (retry < config_.retry_count) {
                usleep(100000);
                continue;
            }
            break;
        }
        
        // 读取累积热量寄存器
        uint16_t registers[2];
        int rc = modbus_read_registers(ctx_, config_.heat_accumulated_addr, 
                                      config_.heat_accumulated_len, registers);
        if (rc == config_.heat_accumulated_len) {
            // 解析32位长整型数据（大端模式）
            uint32_t raw_value = (static_cast<uint32_t>(registers[0]) << 16) | registers[1];
            
            new_data.accumulated_heat = raw_value;
            new_data.accumulated_heat_kwh = static_cast<double>(raw_value) * config_.multiplier;
            new_data.success = true;
            new_data.retry_count = retry;
            
            if (config_.enable_log) {
                LOG_DEBUG(LOGGER_CONSOLE, "Heat meter {}: raw={}, calculated={:.2f} kWh", 
                         config_.meter_address, raw_value, new_data.accumulated_heat_kwh);
            }
            
            break;
        } else {
            new_data.error_message = "Read failed: " + std::string(modbus_strerror(errno));
            
            if (errno == ECONNRESET || errno == EBADF) {
                LOG_INFO(LOGGER_CONSOLE, "Modbus connection broken, reconnecting");
                if (!reconnectModbus()) {
                    break;
                }
            }
            
            if (retry < config_.retry_count) {
                LOG_WARN(LOGGER_CONSOLE, "Retry {}/{} for heat meter {}", 
                        retry + 1, config_.retry_count + 1, config_.meter_address);
                usleep(100000);
                continue;
            }
        }
    }
    
    // 更新数据
    meter_data_ = new_data;
    
    if (new_data.success) {
        LOG_INFO(LOGGER_CONSOLE, "Heat meter {}: {:.2f} kWh", 
                 config_.meter_address, new_data.accumulated_heat_kwh);
    } else {
        LOG_WARN(LOGGER_CONSOLE, "Heat meter {}: read failed - {}", 
                 config_.meter_address, new_data.error_message);
    }
    
    return new_data.success;
}

// 获取当前数据（线程安全）
HeatMeterData HeatMeterReader::getCurrentData() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return meter_data_;
}

// 主轮询逻辑
void HeatMeterReader::run() {
    LOG_INFO(LOGGER_CONSOLE, "Heat meter reader starting");
    
    http_thread_ = std::thread([this]() {
        LOG_INFO(LOGGER_CONSOLE, "HTTP server starting on {}:{}", config_.http_host, config_.http_port);
        if (!http_server_.listen(config_.http_host.c_str(), config_.http_port)) {
            LOG_ERROR(LOGGER_CONSOLE, "HTTP server failed on port {}", config_.http_port);
        } else {
            LOG_INFO(LOGGER_CONSOLE, "HTTP server stopped");
        }
    });
    
    auto last_read_time = std::chrono::steady_clock::now();
    
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_read_time);
        
        if (elapsed.count() >= config_.read_interval_ms) {
            readHeatMeter();
            last_read_time = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Heat meter reader stopped");
}

// 启动服务
bool HeatMeterReader::start() {
    if (running_) {
        LOG_WARN(LOGGER_CONSOLE, "Service already running");
        return false;
    }

    running_ = true;
    main_thread_ = std::thread(&HeatMeterReader::run, this);
    LOG_INFO(LOGGER_CONSOLE, "Heat meter service started");
    return true;
}

// 停止服务
void HeatMeterReader::stop() {
    if (!running_) {
        return;
    }

    LOG_INFO(LOGGER_CONSOLE, "Stopping heat meter service...");
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
    
    LOG_INFO(LOGGER_CONSOLE, "Heat meter service stopped");
}
