#pragma once
#ifndef PHONECAM_LOGGER_H
#define PHONECAM_LOGGER_H

/**
 * Production-grade logging system with file rotation,
 * structured logging, and log levels.
 */

#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <map>
#include <memory>
#include <cstdarg>

namespace phonecam {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERR = 4,
    FATAL = 5
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    // Initialize logger with file path
    bool initialize(const std::string& logDir = ".", LogLevel minLevel = LogLevel::INFO);

    // Log with format string (printf-style)
    void log(LogLevel level, const char* fmt, ...);

    // Log with context map (structured logging)
    void log(LogLevel level, const std::string& message, 
             const std::map<std::string, std::string>& context = {});

    // Convenience methods
    void trace(const std::string& msg) { log(LogLevel::TRACE, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) { log(LogLevel::ERR, msg); }
    void fatal(const std::string& msg) { log(LogLevel::FATAL, msg); }

    void trace(const std::string& msg, const std::map<std::string, std::string>& context) { log(LogLevel::TRACE, msg, context); }
    void debug(const std::string& msg, const std::map<std::string, std::string>& context) { log(LogLevel::DEBUG, msg, context); }
    void info(const std::string& msg, const std::map<std::string, std::string>& context) { log(LogLevel::INFO, msg, context); }
    void warn(const std::string& msg, const std::map<std::string, std::string>& context) { log(LogLevel::WARN, msg, context); }
    void error(const std::string& msg, const std::map<std::string, std::string>& context) { log(LogLevel::ERR, msg, context); }
    void fatal(const std::string& msg, const std::map<std::string, std::string>& context) { log(LogLevel::FATAL, msg, context); }

    void setMinLevel(LogLevel level) { m_minLevel = level; }
    void setConsoleOutput(bool enable) { m_consoleOutput = enable; }
    void setFileOutput(bool enable) { m_fileOutput = enable; }

    ~Logger();

private:
    Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void rotateLogFiles();
    std::string getCurrentLogFilePath() const;
    std::string levelToString(LogLevel level) const;
    std::string getTimestamp() const;
    void writeToConsole(const std::string& message, LogLevel level);
    void writeToFile(const std::string& message);

    std::mutex m_mutex;
    std::unique_ptr<std::ofstream> m_fileStream;
    std::string m_logDir;
    std::string m_logFile;
    LogLevel m_minLevel = LogLevel::INFO;
    bool m_consoleOutput = true;
    bool m_fileOutput = true;
    
    // Log rotation settings
    static constexpr int MAX_LOG_FILES = 10;
    static constexpr int MAX_LOG_SIZE = 100 * 1024 * 1024;  // 100MB
    int m_currentFileSize = 0;
};

// Global convenience functions
inline void log_trace(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Logger::getInstance().trace(buffer);
}

inline void log_debug(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Logger::getInstance().debug(buffer);
}

inline void log_info(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Logger::getInstance().info(buffer);
}

inline void log_warn(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Logger::getInstance().warn(buffer);
}

inline void log_error(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Logger::getInstance().error(buffer);
}

inline void log_fatal(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Logger::getInstance().fatal(buffer);
}

} // namespace phonecam

#endif // PHONECAM_LOGGER_H
