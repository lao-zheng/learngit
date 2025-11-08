/*   #include "electric_meter.h"
#include <csignal>
#include <atomic>

std::atomic<bool> running{true};

void signal_handler(int signal) {
    LOG_INFO(LOGGER_CONSOLE, "Signal {} received, shutting down", signal);
    running = false;
}

bool initializeLogger(Hlog& logger) {
    try {
        logger.AddColorConsole(LOGGER_CONSOLE, logger.GetOutLevelEnum("INFO"));
        logger.AddRotatingFile(LOGGER_CONSOLE, LOG_FILEDIR);
        logger.Init();
        LOG_INFO(LOGGER_CONSOLE, "Logger initialized");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Logger init failed: " << e.what() << std::endl;
        return false;
    }
}

void registerSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
    
    LOG_INFO(LOGGER_CONSOLE, "Signal handlers registered");
}

int main() {
    // 初始化日志
    Hlog logger;
    if (!initializeLogger(logger)) {
        return -1;
    }
    
    // 注册信号处理器
    registerSignalHandlers();
    
    // 创建电表读取器
    ElectricMeterReader reader(ElectricConfig{});
    
    // 加载配置
    std::string config_file = TOML_FILEDIR;
    if (!reader.loadConfig(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed, using defaults");
    }
    
    // 初始化
    if (!reader.initialize()) {
        LOG_ERROR(LOGGER_CONSOLE, "Initialization failed");
        return -1;
    }
    
    // 启动服务
    if (!reader.start()) {
        LOG_ERROR(LOGGER_CONSOLE, "Startup failed");
        return -1;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Service running, press Ctrl+C to stop");
    
    // 主循环等待信号
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // 停止服务
    reader.stop();
    LOG_INFO(LOGGER_CONSOLE, "Program exited");
    
    return 0;
} 
 


  */

/* 
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <csignal>
#include <cstring>
#include "modbus.h"

std::atomic<bool> running{true};
std::atomic<bool> force_quit{false};

void signal_handler(int signal) {
    std::cout << "Signal " << signal << " received, shutting down..." << std::endl;
    running = false;
    force_quit = true;
}

class ModbusTester {
private:
    modbus_t* ctx_;
    std::string host_;
    int port_;
    int slave_id_;
    std::mutex modbus_mutex_;
    int timeout_ms_;

public:
    ModbusTester(const std::string& host, int port, int slave_id = 1, int timeout_ms = 3000) 
        : host_(host), port_(port), slave_id_(slave_id), timeout_ms_(timeout_ms), ctx_(nullptr) {
    }

    ~ModbusTester() {
        disconnect();
    }

    bool connect() {
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        
        if (ctx_) {
            modbus_close(ctx_);
            modbus_free(ctx_);
            ctx_ = nullptr;
        }

        if (force_quit) {
            return false;
        }

        std::cout << "Connecting to " << host_ << ":" << port_ << "..." << std::endl;
        ctx_ = modbus_new_tcp(host_.c_str(), port_);
        if (!ctx_) {
            std::cout << "Failed to create Modbus context" << std::endl;
            return false;
        }

        // 设置超时
        modbus_set_response_timeout(ctx_, timeout_ms_ / 1000, (timeout_ms_ % 1000) * 1000);
        modbus_set_byte_timeout(ctx_, 1, 0);
        modbus_set_slave(ctx_, slave_id_);

        if (modbus_connect(ctx_) == -1) {
            std::cout << "Connection failed: " << modbus_strerror(errno) << std::endl;
            modbus_free(ctx_);
            ctx_ = nullptr;
            return false;
        }

        std::cout << "Connected successfully!" << std::endl;
        return true;
    }

    void disconnect() {
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        if (ctx_) {
            modbus_close(ctx_);
            modbus_free(ctx_);
            ctx_ = nullptr;
        }
        std::cout << "Disconnected" << std::endl;
    }

    bool isConnected() {
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        return ctx_ != nullptr && modbus_get_socket(ctx_) != -1;
    }

    bool readRegisters(int start_addr, int count, uint16_t* registers) {
        std::lock_guard<std::mutex> lock(modbus_mutex_);
        
        if (!ctx_ || force_quit) {
            return false;
        }

        int rc = modbus_read_registers(ctx_, start_addr, count, registers);
        if (rc == count) {
            return true;
        } else {
            std::cout << "Read failed: " << modbus_strerror(errno) << std::endl;
            return false;
        }
    }

    bool testConnection() {
        uint16_t test_reg;
        return readRegisters(0, 1, &test_reg);
    }

    void autoReconnectTest() {
        int read_count = 0;
        int success_count = 0;
        int fail_count = 0;

        while (running && !force_quit) {
            read_count++;
            
            // 检查连接状态
            if (!isConnected()) {
                std::cout << "[" << read_count << "] Connection lost, reconnecting..." << std::endl;
                if (!connect()) {
                    fail_count++;
                    std::cout << "[" << read_count << "] Reconnect failed, waiting 2 seconds..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
            }

            // 测试读取
            uint16_t registers[18];
            if (readRegisters(0, 18, registers)) {
                success_count++;
                std::cout << "[" << read_count << "] Read successful: ";
                for (int i = 0; i < 9; i++) {
                    float value = parseFloat(&registers[i*2]);
                    std::cout << "M" << (i+1) << ":" << value << " ";
                }
                std::cout << std::endl;
            } else {
                fail_count++;
                std::cout << "[" << read_count << "] Read failed, will reconnect next cycle" << std::endl;
                disconnect(); // 断开连接，下次循环会重连
            }

            // 统计信息
            if (read_count % 10 == 0) {
                std::cout << "=== Statistics: Total=" << read_count 
                          << ", Success=" << success_count 
                          << ", Failed=" << fail_count 
                          << ", Success Rate=" << (success_count * 100.0 / read_count) << "% ===" << std::endl;
            }

            // 等待1秒
            for (int i = 0; i < 10; i++) {
                if (!running || force_quit) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::cout << "Test finished. Final statistics: Total=" << read_count 
                  << ", Success=" << success_count << ", Failed=" << fail_count << std::endl;
    }

private:
    float parseFloat(const uint16_t* regs) {
        union {
            uint32_t i;
            float f;
        } converter;
        
        converter.i = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
        return converter.f;
    }
};

int main() {
    // 注册信号处理器
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    std::cout << "Modbus TCP Tester" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    // 创建测试器
    ModbusTester tester("192.168.1.74", 502, 1, 2000); // 2秒超时

    // 初始连接
    if (!tester.connect()) {
        std::cout << "Initial connection failed, but will continue with auto-reconnect..." << std::endl;
    }

    // 开始自动重连测试
    tester.autoReconnectTest();

    std::cout << "Program exited cleanly" << std::endl;
    return 0;
} */



#include "electric_meter.h"


bool initializeLogger(Hlog& logger) {
    try {
        logger.AddColorConsole(LOGGER_CONSOLE, logger.GetOutLevelEnum("INFO"));
        logger.AddRotatingFile(LOGGER_CONSOLE, LOG_FILEDIR);
        logger.Init();
        LOG_INFO(LOGGER_CONSOLE, "Logger initialized");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Logger init failed: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    // 初始化日志
    Hlog logger;
    if (!initializeLogger(logger)) {
        return -1;
    }
    
    // 创建电表读取器
    ElectricMeterReader reader(ElectricConfig{});
    
    // 加载配置
    std::string config_file = TOML_FILEDIR;
    if (!reader.loadConfig(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed, using defaults");
    }
    
    // 初始化
    if (!reader.initialize()) {
        LOG_ERROR(LOGGER_CONSOLE, "Initialization failed");
        return -1;
    }
    
    // 启动服务（这会阻塞在主线程）
    if (!reader.start()) {
        LOG_ERROR(LOGGER_CONSOLE, "Startup failed");
        return -1;
    }
    
    return 0;
}
