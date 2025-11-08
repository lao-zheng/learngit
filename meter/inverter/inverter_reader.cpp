#include "inverter_reader.h"
#include <csignal>
#include <filesystem>
#include "hlog.h"

// 构造函数
InverterReader::InverterReader(const Config& config) : config_(config) {
    ctx_ = nullptr;
    current_inverter_index_ = 0;
    setDefaultConfig();    
}

// 析构函数
InverterReader::~InverterReader() {
    stop();
}

// 设置默认配置
void InverterReader::setDefaultConfig() {
    // HTTP默认配置
    config_.http_host = "0.0.0.0";
    config_.http_port = 5004;
    config_.http_routes = "/api/collect/v1/photovoltaicMeter/totalKWH/all";
    
    // Modbus默认配置
    config_.rtu_device = "/dev/ttysWK1";
    config_.rtu_baudrate = 9600;
    config_.rtu_parity = "N";
    config_.rtu_data_bits = 8;
    config_.rtu_stop_bits = 1;
    
    // 逆变器默认配置
    config_.inverter_count = 3;
    config_.inverters = {
        {27, "photovoltaic_inverter_1", 0.0, false, 0, 1670, 0.1, std::chrono::system_clock::now()},
        {28, "photovoltaic_inverter_2", 0.0, false, 0, 1670, 0.1, std::chrono::system_clock::now()},
        {35, "huawei_Inverter", 0.0, false, 0, 32106, 0.01, std::chrono::system_clock::now()}
    };
    
    // 读取默认配置
    config_.read_interval_ms = 10000;
    config_.max_retry_count = 3;
    config_.response_timeout_ms = 2000;
    config_.read_timeout_ms = config_.response_timeout_ms;
    config_.enable_logging = true;
    config_.log_filedir = LOG_FILEDIR;
}

// 加载配置文件
bool InverterReader::loadConfig(const std::string& config_file) {
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

        // 读取Modbus RTU配置
        auto modbus_rtu = config_toml["modbus_rtu"];
        config_.rtu_device = modbus_rtu["device"].value_or(config_.rtu_device);
        config_.rtu_baudrate = modbus_rtu["baudrate"].value_or(config_.rtu_baudrate);
        config_.rtu_parity = modbus_rtu["parity"].value_or(config_.rtu_parity);
        config_.rtu_data_bits = modbus_rtu["data_bits"].value_or(config_.rtu_data_bits);
        config_.rtu_stop_bits = modbus_rtu["stop_bits"].value_or(config_.rtu_stop_bits);

// 读取逆变器配置
auto inverter = config_toml["inverter"];
config_.inverter_count = inverter["count"].value_or(config_.inverter_count);

// 清空原有配置
config_.inverters.clear();

// 读取逆变器数组配置 - 修复contains方法问题
auto inverters_node = inverter["inverters"];
if (inverters_node && inverters_node.is_array()) {
    auto inverter_array = inverters_node.as_array();
    if (inverter_array) {
        for (auto& inv : *inverter_array) {
            if (inv.is_table()) {
                auto inv_table = inv.as_table();
                int address = inv_table->get("address")->value_or(1);
                std::string name = inv_table->get("name")->value_or("Inverter_" + std::to_string(address));
                int register_addr = inv_table->get("register_addr")->value_or(1670);
                double multiplier = inv_table->get("multiplier")->value_or(0.1);
                
                config_.inverters.push_back({
                    address, name, 0.0, false, 0, 
                    register_addr, multiplier, std::chrono::system_clock::now()
                });
            }
        }
    }
} else {
    // 使用默认逆变器配置
    config_.inverters = {
        {27, "photovoltaic_inverter_1", 0.0, false, 0, 1670, 0.1, std::chrono::system_clock::now()},
        {28, "photovoltaic_inverter_2", 0.0, false, 0, 1670, 0.1, std::chrono::system_clock::now()},
        {35, "huawei_Inverter", 0.0, false, 0, 32106, 0.01, std::chrono::system_clock::now()}
    };
}

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
bool InverterReader::reconnectModbus() {
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

// 根据逆变器ID查找配置
InverterData* InverterReader::findInverterConfig(int slave_id) {
    for (auto& inv : config_.inverters) {
        if (inv.id == slave_id) {
            return &inv;
        }
    }
    return nullptr;
}

// 初始化函数
bool InverterReader::initialize() {
    // 创建Modbus RTU上下文   
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
    
    // 初始化逆变器数据
    {
        std::unique_lock<std::shared_mutex> lock(inverters_mutex_);
        inverters_.clear();
        for (const auto& inv_cfg : config_.inverters) {
            inverters_.push_back(inv_cfg);
        }
    }
    
    // 设置HTTP路由
    setupHttpRoutes();
    
    last_read_time_ = std::chrono::steady_clock::now();
    current_inverter_index_ = 0;
    
    LOG_INFO(LOGGER_CONSOLE, "Initialized: device={}, inverters={}", config_.rtu_device, inverters_.size());
    return true;
}


// 设置HTTP路由
void InverterReader::setupHttpRoutes() {
    // 获取所有逆变器数据
    http_server_.Get(config_.http_routes, [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json response;
        nlohmann::json inverter_values = nlohmann::json::array();
        
        {
            std::shared_lock<std::shared_mutex> lock(inverters_mutex_);
            for (const auto& inverter : inverters_) {
                if (inverter.success) {
                    // 保留两位小数
                    double rounded_value = std::round(inverter.generation * 100.0) / 100.0;
                    inverter_values.push_back(rounded_value);
                } else {
                    inverter_values.push_back(-1);
                }
            }
        }
        
        response["message"] = inverter_values;
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
        
        result["inverter_count"] = inverters_.size();
        result["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        res.set_content(result.dump(), "application/json");
    });
    
    // 单个逆变器查询
    http_server_.Get("/api/collect/v1/inverter/totalT/:id", [this](const httplib::Request& req, httplib::Response& res) {
        int inverter_id;
        try {
            inverter_id = std::stoi(req.path_params.at("id"));
        } catch (const std::exception& e) {
            nlohmann::json error_response;
            error_response["message"] = "error: invalid inverter id";
            error_response["result"] = -1;
            error_response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            res.set_content(error_response.dump(), "application/json");
            return;
        }
        
        nlohmann::json response;
        nlohmann::json inverter_value = nlohmann::json::array();

        std::shared_lock<std::shared_mutex> lock(inverters_mutex_);
        auto it = std::find_if(inverters_.begin(), inverters_.end(), 
                  [inverter_id](const InverterData& inv) { return inv.id == inverter_id; });

        if (it != inverters_.end()) {
            const InverterData& cached_data = *it;
            if (cached_data.success) {
                // 保留两位小数
                double rounded_value = std::round(cached_data.generation * 100.0) / 100.0;
                inverter_value.push_back(rounded_value);
            } else {
                inverter_value.push_back(-1);
            }
            response["result"] = 0;
        } else {
            inverter_value.push_back(-1);
            response["result"] = -1;
        }

        response["message"] = inverter_value;
        response["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        
        res.set_content(response.dump(), "application/json");
    });
}

// 读取单个逆变器
InverterData InverterReader::readSingleInverter(int slave_id, int register_addr, double multiplier) {
    std::lock_guard<std::mutex> lock(modbus_mutex_);

    InverterData* inv_cfg = findInverterConfig(slave_id);
    InverterData result{slave_id, inv_cfg ? inv_cfg->name : "Unknown", 0.0, false, 0, 
                       register_addr, multiplier, std::chrono::system_clock::now()};
    
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
        
        uint16_t regs[2];
        int rc = modbus_read_registers(ctx_, register_addr, 2, regs);
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
        
        
        // 简化修复：确保使用正确的数据类型
        uint32_t raw_value = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];

        // 使用高精度计算避免浮点误差
        result.generation = static_cast<double>(raw_value) * multiplier;

        // 四舍五入到合适的小数位数
        result.generation = std::round(result.generation * 100.0) / 100.0;  // 保留2位小数

        // 解析32位数据 (高位在前)
        uint32_t value = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
        result.generation = value * multiplier; 
        result.success = true;
        result.retry_count = retry;
        break;
    }
    
    return result;
}
  
// 读取下一个逆变器
void InverterReader::readNextInverter() {
    if (inverters_.empty()) return;
    
    auto& inverter = inverters_[current_inverter_index_];
    InverterData new_data = readSingleInverter(inverter.id, inverter.register_addr, inverter.multiplier);
    
    {
        std::unique_lock<std::shared_mutex> lock(inverters_mutex_);
        inverter.generation = new_data.generation;
        inverter.success = new_data.success;
        inverter.retry_count = new_data.retry_count;
        inverter.last_update = new_data.last_update;
    }
    
    if (inverter.success) {
        LOG_INFO(LOGGER_CONSOLE, "Inverter {} ({}): {:.2f} kWh", inverter.id, inverter.name, inverter.generation);
    } else {
        LOG_WARN(LOGGER_CONSOLE, "Inverter {} ({}): read failed", inverter.id, inverter.name);
    }

    current_inverter_index_ = (current_inverter_index_ + 1) % inverters_.size();
}

// 主轮询逻辑
void InverterReader::run() {
    LOG_INFO(LOGGER_CONSOLE, "Inverter reader starting");
    
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
            readNextInverter();
            last_read_time_ = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Inverter reader stopped");
}

// 启动服务
bool InverterReader::start() {
    if (running_) {
        LOG_WARN(LOGGER_CONSOLE, "Service already running");
        return false;
    }

    running_ = true;
    main_thread_ = std::thread(&InverterReader::run, this);
    LOG_INFO(LOGGER_CONSOLE, "Service started");
    return true;
}

// 停止服务
void InverterReader::stop() {
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