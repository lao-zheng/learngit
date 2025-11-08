#ifndef _MODBUS_BASE_H_
#define _MODBUS_BASE_H_

#include "modbus.h"
#include <string>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstring>
#include <cerrno>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>

/* // 自定义异常类，包含更详细的错误信息
class ModbusBaseException : public std::runtime_error {
public:
    ModbusBaseException(const std::string& msg, int error_code = 0) 
        : std::runtime_error(msg), error_code_(error_code) {}
    
    int getErrorCode() const { return error_code_; }

private:
    int error_code_; // 存储原始错误码
};

class ModbusBase {
protected:
    modbus_t* m_ctx = nullptr;
    int m_slave_id = -1;
    bool m_connected = false;
    ConnectionType m_conn_type; // 新增：存储连接类型
    
    // 连接参数
    std::string m_connection_str;
    int m_baudrate = 9600;
    char m_parity = 'N';
    int m_data_bit = 8;
    int m_stop_bit = 1;
    
    // 超时设置 (毫秒)
    int m_response_timeout = 1000;
    int m_byte_timeout = 1000;
    
    // 线程安全相关
    mutable std::mutex m_mutex;
    int m_retry_count = 1; // 操作失败后的重试次数
    
public:
    enum ConnectionType {
        RTU,
        TCP
    };
    
    ModbusBase() = default;
    virtual ~ModbusBase() {
        disconnect();
    }
    
    // 删除拷贝构造函数和赋值运算符
    ModbusBase(const ModbusBase&) = delete;
    ModbusBase& operator=(const ModbusBase&) = delete;
    
    // 设置超时参数
    void setTimeouts(int response_timeout_ms, int byte_timeout_ms) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_response_timeout = response_timeout_ms;
        m_byte_timeout = byte_timeout_ms;
        
        if (m_ctx) {
            modbus_set_response_timeout(m_ctx, 
                                       m_response_timeout / 1000, 
                                      (m_response_timeout % 1000) * 1000);
            modbus_set_byte_timeout(m_ctx, 
                                   m_byte_timeout / 1000, 
                                  (m_byte_timeout % 1000) * 1000);
        }
    }
    
    // 设置操作失败后的重试次数
    void setRetryCount(int count) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (count >= 0) {
            m_retry_count = count;
        }
    }
    
    // 建立连接
    virtual void connect(ConnectionType type, const std::string& connection_str, 
                        int slave_id = -1, int baudrate = 9600, char parity = 'N', 
                        int data_bit = 8, int stop_bit = 1) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // 如果已连接，先断开
        if (m_connected) {
            disconnect();
        }
        
        // 验证连接参数
        if (!validateConnectionParams(type, connection_str, baudrate, parity, data_bit, stop_bit)) {
            throw ModbusBaseException("Invalid connection parameters");
        }
        
        // 保存连接参数
        m_conn_type = type;
        m_connection_str = connection_str;
        m_slave_id = slave_id;
        m_baudrate = baudrate;
        m_parity = parity;
        m_data_bit = data_bit;
        m_stop_bit = stop_bit;
        
        try {
            // 创建上下文
            if (type == RTU) {
                m_ctx = modbus_new_rtu(connection_str.c_str(), baudrate, parity, 
                                      data_bit, stop_bit);
            } else {
                // TCP连接格式: "ip:port" 或 "ip"
                size_t colon_pos = connection_str.find(':');
                if (colon_pos != std::string::npos) {
                    std::string ip = connection_str.substr(0, colon_pos);
                    int port = std::stoi(connection_str.substr(colon_pos + 1));
                    m_ctx = modbus_new_tcp(ip.c_str(), port);
                } else {
                    m_ctx = modbus_new_tcp(connection_str.c_str(), 502); // 默认端口
                }
            }
            
            if (m_ctx == nullptr) {
                throw ModbusBaseException("Failed to create Modbus context", errno);
            }
            
            // 设置超时
            modbus_set_response_timeout(m_ctx, m_response_timeout / 1000, 
                                      (m_response_timeout % 1000) * 1000);
            modbus_set_byte_timeout(m_ctx, m_byte_timeout / 1000, 
                                  (m_byte_timeout % 1000) * 1000);
            
            // 设置从站ID (RTU和TCP都支持)
            if (slave_id >= 0) {
                if (modbus_set_slave(m_ctx, slave_id) != 0) {
                    throw ModbusBaseException("Failed to set slave ID", errno);
                }
            }
            
            // 建立连接
            if (modbus_connect(m_ctx) != 0) {
                throw ModbusBaseException("Connection failed: " + 
                                         std::string(modbus_strerror(errno)), errno);
            }
            
            m_connected = true;
            std::cout << "Successfully connected to " << connection_str << std::endl;
            
        } catch (const std::exception& e) {
            if (m_ctx) {
                modbus_free(m_ctx);
                m_ctx = nullptr;
            }
            throw ModbusBaseException(std::string("Connection error: ") + e.what(), errno);
        }
    }
    
    // 断开连接
    virtual void disconnect() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_ctx) {
            modbus_close(m_ctx);
            modbus_free(m_ctx);
            m_ctx = nullptr;
        }
        m_connected = false;
        std::cout << "Disconnected from " << m_connection_str << std::endl;
    }
    
    // 重新连接
    virtual bool reconnect() {
        // 注意: 这里不加锁，避免死锁
        // 调用者应确保线程安全
        
        if (m_connection_str.empty()) {
            std::cerr << "No connection parameters available for reconnection" << std::endl;
            return false;
        }
        
        // 保存参数，因为disconnect会清空部分状态
        ConnectionType type = m_conn_type;
        std::string conn_str = m_connection_str;
        int slave_id = m_slave_id;
        int baudrate = m_baudrate;
        char parity = m_parity;
        int data_bit = m_data_bit;
        int stop_bit = m_stop_bit;
        
        disconnect();
        
        // 重连策略：最多尝试3次，每次间隔1秒
        for (int i = 0; i < 3; ++i) {
            try {
                // 使用保存的连接类型和参数重新连接
                connect(type, conn_str, slave_id, baudrate, parity, data_bit, stop_bit);
                return true;
            } catch (const ModbusBaseException& e) {
                std::cerr << "Reconnect attempt " << (i + 1) << " failed: " 
                          << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        return false;
    }
    
    // 读取保持寄存器
    virtual int readHoldingRegisters(int addr, int nb, uint16_t* dest) {
        return performWithRetry([&]() {
            return modbus_read_registers(m_ctx, addr, nb, dest);
        }, "read holding registers", addr, nb);
    }
    
    // 读取输入寄存器
    virtual int readInputRegisters(int addr, int nb, uint16_t* dest) {
        return performWithRetry([&]() {
            return modbus_read_input_registers(m_ctx, addr, nb, dest);
        }, "read input registers", addr, nb);
    }
    
    // 读取线圈状态
    virtual int readCoils(int addr, int nb, uint8_t* dest) {
        return performWithRetry([&]() {
            return modbus_read_bits(m_ctx, addr, nb, dest);
        }, "read coils", addr, nb);
    }
    
    // 读取输入状态
    virtual int readDiscreteInputs(int addr, int nb, uint8_t* dest) {
        return performWithRetry([&]() {
            return modbus_read_input_bits(m_ctx, addr, nb, dest);
        }, "read discrete inputs", addr, nb);
    }
    
    // 写入单个线圈
    virtual int writeCoil(int addr, int status) {
        return performWithRetry([&]() {
            return modbus_write_bit(m_ctx, addr, status);
        }, "write coil", addr, 1);
    }
    
    // 写入多个线圈 (新增)
    virtual int writeCoils(int addr, int nb, const uint8_t* data) {
        return performWithRetry([&]() {
            return modbus_write_bits(m_ctx, addr, nb, data);
        }, "write multiple coils", addr, nb);
    }
    
    // 写入单个寄存器
    virtual int writeRegister(int addr, uint16_t value) {
        return performWithRetry([&]() {
            return modbus_write_register(m_ctx, addr, value);
        }, "write register", addr, 1);
    }
    
    // 写入多个寄存器
    virtual int writeRegisters(int addr, int nb, const uint16_t* data) {
        return performWithRetry([&]() {
            return modbus_write_registers(m_ctx, addr, nb, data);
        }, "write multiple registers", addr, nb);
    }
    
    // 检查连接状态
    bool isConnected() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_connected;
    }
    
    // 获取连接信息
    std::string getConnectionInfo() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_connection_str;
    }
    
protected:
    // 验证连接参数的有效性
    bool validateConnectionParams(ConnectionType type, const std::string& conn_str,
                                 int baudrate, char parity, int data_bit, int stop_bit) {
        if (conn_str.empty()) {
            std::cerr << "Connection string cannot be empty" << std::endl;
            return false;
        }
        
        if (type == RTU) {
            // 检查RTU设备路径是否存在
            struct stat buffer;
            if (stat(conn_str.c_str(), &buffer) != 0) {
                std::cerr << "RTU device not found: " << conn_str << std::endl;
                return false;
            }
            
            // 检查波特率是否合理
            const int valid_baudrates[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
            bool valid_baud = false;
            for (int b : valid_baudrates) {
                if (baudrate == b) {
                    valid_baud = true;
                    break;
                }
            }
            if (!valid_baud) {
                std::cerr << "Invalid baudrate: " << baudrate << std::endl;
                return false;
            }
            
            // 检查数据位
            if (data_bit < 5 || data_bit > 8) {
                std::cerr << "Invalid data bits: " << data_bit << std::endl;
                return false;
            }
            
            // 检查停止位
            if (stop_bit < 1 || stop_bit > 2) {
                std::cerr << "Invalid stop bits: " << stop_bit << std::endl;
                return false;
            }
            
            // 检查校验位
            if (parity != 'N' && parity != 'O' && parity != 'E') {
                std::cerr << "Invalid parity: " << parity << std::endl;
                return false;
            }
        } else {
            // 检查TCP IP地址格式
            size_t colon_pos = conn_str.find(':');
            std::string ip = (colon_pos != std::string::npos) ? 
                            conn_str.substr(0, colon_pos) : conn_str;
            
            struct sockaddr_in sa;
            if (inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 1) {
                std::cerr << "Invalid IP address: " << ip << std::endl;
                return false;
            }
            
            // 检查端口号
            if (colon_pos != std::string::npos) {
                try {
                    int port = std::stoi(conn_str.substr(colon_pos + 1));
                    if (port < 1 || port > 65535) {
                        std::cerr << "Invalid port number: " << port << std::endl;
                        return false;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Invalid port format: " << e.what() << std::endl;
                    return false;
                }
            }
        }
        
        return true;
    }
    
    // 带重试机制的操作执行器
    template <typename Func>
    int performWithRetry(Func func, const std::string& operation, int addr, int nb) {
        int attempt = 0;
        while (attempt <= m_retry_count) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                
                if (!m_connected) {
                    throw ModbusBaseException("Not connected to Modbus device");
                }
                
                int result = func();
                if (result != -1) {
                    return result;
                }
                
                // 保存错误信息
                int current_errno = errno;
                std::string error_msg = modbus_strerror(current_errno);
                
                // 判断是否是连接错误
                const int connection_errors[] = {
                    EMBXILFUN, EMBXCMD, EMBXMDATA, EMBBADCRC,
                    ETIMEDOUT, ECONNREFUSED, ECONNRESET, ECONNABORTED,
                    ENETUNREACH, ENETDOWN, EHOSTUNREACH
                };
                
                bool is_connection_error = false;
                for (int err : connection_errors) {
                    if (current_errno == err) {
                        is_connection_error = true;
                        break;
                    }
                }
                
                if (is_connection_error) {
                    m_connected = false;
                }
                
                // 如果是最后一次尝试，抛出异常
                if (attempt == m_retry_count) {
                    std::string full_msg = operation + " failed [addr=" + std::to_string(addr) + 
                                          ", count=" + std::to_string(nb) + "]: " + error_msg;
                    throw ModbusBaseException(full_msg, current_errno);
                }
            } // 释放锁
            
            // 如果是连接错误，尝试重连
            if (!m_connected) {
                std::cerr << "Connection error during " << operation 
                          << ", attempting to reconnect..." << std::endl;
                
                if (reconnect()) {
                    std::cerr << "Reconnected successfully" << std::endl;
                } else {
                    std::string full_msg = operation + " failed and reconnection failed [addr=" + 
                                          std::to_string(addr) + ", count=" + std::to_string(nb) + "]";
                    throw ModbusBaseException(full_msg, errno);
                }
            }
            
            std::cerr << "Retrying " << operation << " (attempt " << attempt + 1 << ")" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
            attempt++;
        }
        
        throw ModbusBaseException("All retry attempts failed for " + operation);
    }
}; */

#endif // _MODBUS_BASE_H_