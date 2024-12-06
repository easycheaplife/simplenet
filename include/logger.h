#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <ctime>
#include <memory>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger {
  public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    template<typename... Args>
    void log(LogLevel level, const char* file, int line, const char* fmt, Args... args) {
        std::lock_guard<std::mutex> lock(mutex_);
		if (level < logLevel_) { return; }
        std::stringstream ss;
        ss << getCurrentTime() << " "
           << getLevelStr(level) << " "
           << "[" << file << ":" << line << "] ";

        format(ss, fmt, args...);
        ss << "\n";

        std::string msg = ss.str();
        if (outputFile_.is_open()) {
            outputFile_ << msg;
            outputFile_.flush();
        }
        std::cout << msg;
    }

    bool init(const std::string& filename, LogLevel level = LogLevel::INFO) {
        outputFile_.open(filename, std::ios::app);
		logLevel_ = level;
        return outputFile_.is_open();
    }

  private:
    Logger() = default;

    static std::string getCurrentTime() {
        char buffer[32];
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        return buffer;
    }

    static const char* getLevelStr(LogLevel level) {
        switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO ";
        case LogLevel::WARN:
            return "WARN ";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    template<typename T>
    void format(std::stringstream& ss, const char* fmt, T value) {
        while (*fmt) {
            if (*fmt == '{' && *(fmt + 1) == '}') {
                ss << value;
                fmt += 2;
                return;
            }
            ss << *fmt++;
        }
    }

    template<typename T, typename... Args>
    void format(std::stringstream& ss, const char* fmt, T value, Args... args) {
        while (*fmt) {
            if (*fmt == '{' && *(fmt + 1) == '}') {
                ss << value;
                fmt += 2;
                format(ss, fmt, args...);
                return;
            }
            ss << *fmt++;
        }
    }

    void format(std::stringstream& ss, const char* fmt) {
        while (*fmt) {
            ss << *fmt++;
        }
    }

    std::mutex mutex_;
    std::ofstream outputFile_;
	LogLevel logLevel_;
};

#define LOG_DEBUG(fmt, ...) \
    Logger::instance().log(LogLevel::DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) \
    Logger::instance().log(LogLevel::INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) \
    Logger::instance().log(LogLevel::WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) \
    Logger::instance().log(LogLevel::ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) \
    Logger::instance().log(LogLevel::FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
