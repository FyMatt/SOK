#pragma once
#include <mutex>
#include <fstream>
#include <iostream>
#include <string>
#include <ctime>
#include <sstream>

namespace SOK {
class Logger {
public:
    enum Level { INFO, WARNING, ERROR };
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void set_max_filesize(size_t bytes) { max_filesize_ = bytes; }

    /// @brief 设置日志文件
    /// @param filename 
    void set_logfile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ofs_.is_open()) ofs_.close();
        log_filename_ = filename;
        std::cout << "[Logger] set_logfile called with: " << filename << std::endl;
        if (filename.empty()) {
            std::cerr << "[Logger] 日志文件名为空，仅输出到控制台。" << std::endl;
            return;
        }
        ofs_.open(filename, std::ios::app);
        if (!ofs_.is_open()) {
            std::cerr << "[Logger] 无法打开日志文件：" << filename << "，仅输出到控制台。" << std::endl;
        } else {
            std::cout << "[Logger] 日志文件打开成功: " << filename << std::endl;
        }
    }

    /// @brief 输出日志, 设置日志级别
    /// @param level 
    /// @param msg 
    void log(Level level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        check_and_rotate();
        std::ostringstream oss;
        oss << now() << " [" << level_str(level) << "] " << msg << "\n";
        if (ofs_.is_open()) {
            ofs_ << oss.str();
            ofs_.flush();
        }
        std::cout << oss.str();
    }
    void info(const std::string& msg) { log(INFO, msg); }
    void warn(const std::string& msg) { log(WARNING, msg); }
    void error(const std::string& msg) { log(ERROR, msg); }
private:
    Logger() = default;
    ~Logger() { if (ofs_.is_open()) ofs_.close(); }
    std::string now() {
        std::time_t t = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        return buf;
    }
    const char* level_str(Level l) {
        switch(l) {
            case INFO: return "INFO";
            case WARNING: return "WARN";
            case ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
    void check_and_rotate() {
        if (!log_filename_.empty() && ofs_.is_open()) {
            ofs_.flush();
            ofs_.seekp(0, std::ios::end);
            size_t size = ofs_.tellp();
            if (size >= max_filesize_) {
                ofs_.close();
                std::string ts = now_for_filename();
                std::string newname;
                size_t dot = log_filename_.rfind(".log");
                if (dot != std::string::npos && dot == log_filename_.size() - 4) {
                    newname = log_filename_.substr(0, dot) + "_" + ts + ".log";
                } else {
                    newname = log_filename_ + "_" + ts;
                }
                std::rename(log_filename_.c_str(), newname.c_str());
                ofs_.open(log_filename_, std::ios::app);
            }
        }
    }
    std::string now_for_filename() {
        std::time_t t = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
        return buf;
    }
    std::mutex mutex_;
    std::ofstream ofs_;
    std::string log_filename_;
    size_t max_filesize_ = 10 * 1024 * 1024; // 10MB
};

}
