#include "Logger.h"
#include <filesystem>
#include <windows.h>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace phonecam {

Logger::Logger() {}

Logger::~Logger() {
    if (m_fileStream && m_fileStream->is_open()) {
        m_fileStream->close();
    }
}

bool Logger::initialize(const std::string& logDir, LogLevel minLevel) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_logDir = logDir;
    m_minLevel = minLevel;

    // Create log directory if it doesn't exist
    try {
        std::filesystem::create_directories(logDir);
    } catch (const std::exception& e) {
        fprintf(stderr, "Logger: Failed to create log directory: %s\n", e.what());
        return false;
    }

    // Open log file
    std::string logPath = getCurrentLogFilePath();
    m_fileStream = std::make_unique<std::ofstream>(logPath, std::ios::app);
    
    if (!m_fileStream->is_open()) {
        fprintf(stderr, "Logger: Failed to open log file: %s\n", logPath.c_str());
        return false;
    }

    // Write header
    *m_fileStream << "\n" 
                  << "================================\n"
                  << "PhoneCam Service Log Started\n"
                  << "Time: " << getTimestamp() << "\n"
                  << "Version: 1.0\n"
                  << "================================\n";
    m_fileStream->flush();

    return true;
}

std::string Logger::getCurrentLogFilePath() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << m_logDir << "/phonecam-service-";
    
    char dateStr[32];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", localtime(&time));
    ss << dateStr << ".log";
    
    return ss.str();
}

std::string Logger::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&time));
    ss << timeStr << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

std::string Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERR:   return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "???? ";
    }
}

void Logger::writeToConsole(const std::string& message, LogLevel level) {
    if (!m_consoleOutput) return;

    // Color coding for console output
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    constexpr WORD DEFAULT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    WORD color = DEFAULT_COLOR;

    switch (level) {
        case LogLevel::ERR:
        case LogLevel::FATAL:
            color = FOREGROUND_RED | FOREGROUND_INTENSITY;
            break;
        case LogLevel::WARN:
            color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            break;
        case LogLevel::DEBUG:
            color = FOREGROUND_GREEN;
            break;
        default:
            color = DEFAULT_COLOR;
    }

    SetConsoleTextAttribute(hConsole, color);
    fprintf(stdout, "%s\n", message.c_str());
    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
}

void Logger::writeToFile(const std::string& message) {
    if (!m_fileOutput || !m_fileStream || !m_fileStream->is_open()) {
        return;
    }

    *m_fileStream << message << "\n";
    m_currentFileSize += message.length() + 1;

    // Flush occasionally (every 10KB)
    if (m_currentFileSize > 10 * 1024) {
        m_fileStream->flush();
        m_currentFileSize = 0;

        // Check if we need to rotate
        try {
            auto fileSize = std::filesystem::file_size(getCurrentLogFilePath());
            if (fileSize > MAX_LOG_SIZE) {
                rotateLogFiles();
            }
        } catch (...) {
            // Ignore file size check errors
        }
    }
}

void Logger::rotateLogFiles() {
    // Caller already holds m_mutex via log(); taking it again would deadlock.
    if (m_fileStream) {
        m_fileStream->close();
        m_fileStream = nullptr;
    }

    try {
        // Find all log files for today
        std::string pattern = m_logDir + "/phonecam-service-*.log.";
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        char dateStr[32];
        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", localtime(&time));
        
        // Rename old logs with .1, .2, etc.
        for (int i = MAX_LOG_FILES - 1; i > 0; --i) {
            std::string oldName = m_logDir + "/phonecam-service-" + 
                                 dateStr + ".log." + std::to_string(i);
            std::string newName = m_logDir + "/phonecam-service-" + 
                                 dateStr + ".log." + std::to_string(i + 1);
            
            if (std::filesystem::exists(oldName)) {
                if (i + 1 >= MAX_LOG_FILES) {
                    std::filesystem::remove(oldName);
                } else {
                    std::filesystem::rename(oldName, newName);
                }
            }
        }

        // Rename current log
        std::string currentLog = getCurrentLogFilePath();
        std::string rotatedLog = currentLog + ".1";
        if (std::filesystem::exists(currentLog)) {
            std::filesystem::rename(currentLog, rotatedLog);
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "Logger: Failed to rotate logs: %s\n", e.what());
    }

    // Open new log file
    std::string logPath = getCurrentLogFilePath();
    m_fileStream = std::make_unique<std::ofstream>(logPath, std::ios::app);
    m_currentFileSize = 0;
}

void Logger::log(LogLevel level, const char* fmt, ...) {
    if (level < m_minLevel) return;

    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
    va_end(args);

    log(level, std::string(buffer));
}

void Logger::log(LogLevel level, const std::string& message,
                 const std::map<std::string, std::string>& context) {
    if (level < m_minLevel) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Format: [TIMESTAMP] [LEVEL] message | key1=val1 key2=val2
    std::stringstream ss;
    ss << "[" << getTimestamp() << "] [" << levelToString(level) << "] " 
       << message;

    if (!context.empty()) {
        ss << " |";
        for (const auto& [key, val] : context) {
            ss << " " << key << "=" << val;
        }
    }

    std::string formatted = ss.str();

    writeToConsole(formatted, level);
    writeToFile(formatted);
}

} // namespace phonecam
