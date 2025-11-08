#include "inverter_reader.h"
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
    Hlog logger;
    if (!initializeLogger(logger)) {
        return -1;
    }
    
    registerSignalHandlers();
    
    InverterReader reader(Config{});
    
    std::string config_file = TOML_FILEDIR;
    if (!reader.loadConfig(config_file)) {
        LOG_ERROR(LOGGER_CONSOLE, "Config load failed, using defaults");
    }
    
    if (!reader.initialize()) {
        LOG_ERROR(LOGGER_CONSOLE, "Initialization failed");
        return -1;
    }
    
    if (!reader.start()) {
        LOG_ERROR(LOGGER_CONSOLE, "Startup failed");
        return -1;
    }
    
    LOG_INFO(LOGGER_CONSOLE, "Service running, press Ctrl+C to stop");
    
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    reader.stop();
    LOG_INFO(LOGGER_CONSOLE, "Program exited");
    
    return 0;
}