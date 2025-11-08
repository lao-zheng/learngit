#include "water_meter.h"
#include <csignal>
#include <atomic>

// 全局运行状态标志（原子类型，线程安全：信号处理线程和主线程共享）
std::atomic<bool> running{true};

// 信号处理函数：捕获退出信号（Ctrl+C=SIGINT，kill命令=SIGTERM）
void signal_handler(int signal) {
    LOG_INFO(LOGGER_CONSOLE, "Signal {} received, shutting down", signal);
    running = false; //设置状态为false，主循环会退出
}

// 初始化日志：创建控制台日志（彩色）和滚动日志文件，返回初始化结果
bool initializeLogger(Hlog& logger) {
    try {
        // 添加彩色控制台日志：输出级别INFO（只输出INFO及以上级别日志）
        logger.AddColorConsole(LOGGER_CONSOLE, logger.GetOutLevelEnum("INFO"));
        // 添加滚动日志文件：按大小/时间分割，避免日志文件过大
        logger.AddRotatingFile(LOGGER_CONSOLE, LOG_FILEDIR);
        logger.Init();
        LOG_INFO(LOGGER_CONSOLE, "Logger initialized");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Logger init failed: " << e.what() << std::endl;
        return false;
    }
}

// 注册信号处理器：处理退出信号+忽略SIGPIPE（避免HTTP连接断开导致程序崩溃）
void registerSignalHandlers() {
    struct sigaction sa;
    // 信号处理函数
    sa.sa_handler = signal_handler; 
    // 清空信号掩码（不屏蔽其他信号）
    sigemptyset(&sa.sa_mask);
    // 无特殊标志
    sa.sa_flags = 0;
    
    // 注册SIGINT（Ctrl+C）和SIGTERM（kill命令）的处理
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    // 忽略SIGPIPE（HTTP客户端断开连接时，服务器写数据会触发此信号，默认会崩溃）
    signal(SIGPIPE, SIG_IGN);
    
    LOG_INFO(LOGGER_CONSOLE, "Signal handlers registered");
}

int main() {
    // 初始化日志
    Hlog logger;
    if (!initializeLogger(logger)) {
        return -1;
    }
    
    // 注册信号处理器（确保程序能优雅退出，不残留资源）
    registerSignalHandlers();
    
    // 创建水表读取核心对象（初始配置为空，后续加载TOML）
    WaterMeterReader reader(Config{});
    
    // 加载配置
    std::string config_file = TOML_FILEDIR;
    if (!reader.loadConfig(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed, using defaults");
    }
    
    //初始化Modbus和HTTP服务（失败则程序退出）
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




