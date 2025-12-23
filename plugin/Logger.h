#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include <string>
#include <memory>

namespace KYL {

class Logger {
public:
    enum class Level {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        Critical = 5,
        Off = 6
    };

    static Logger& GetInstance();

    // Initialize logger with INI file configuration
    bool Initialize(const std::filesystem::path& iniPath, const std::filesystem::path& logPath);

    // Get the current log level
    Level GetLogLevel() const { return m_logLevel; }

    // Get the underlying spdlog logger
    std::shared_ptr<spdlog::logger> GetLogger() const { return m_logger; }

    // Convenience logging functions
    template<typename... Args>
    void Trace(fmt::format_string<Args...> fmt, Args&&... args) {
        if (m_logger && m_logLevel <= Level::Trace) {
            m_logger->trace(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Debug(fmt::format_string<Args...> fmt, Args&&... args) {
        if (m_logger && m_logLevel <= Level::Debug) {
            m_logger->debug(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Info(fmt::format_string<Args...> fmt, Args&&... args) {
        if (m_logger && m_logLevel <= Level::Info) {
            m_logger->info(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Warn(fmt::format_string<Args...> fmt, Args&&... args) {
        if (m_logger && m_logLevel <= Level::Warning) {
            m_logger->warn(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Error(fmt::format_string<Args...> fmt, Args&&... args) {
        if (m_logger && m_logLevel <= Level::Error) {
            m_logger->error(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Critical(fmt::format_string<Args...> fmt, Args&&... args) {
        if (m_logger && m_logLevel <= Level::Critical) {
            m_logger->critical(fmt, std::forward<Args>(args)...);
        }
    }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Parse log level from INI file
    Level ParseLogLevel(const std::string& levelStr);

    // Convert our Level enum to spdlog::level::level_enum
    spdlog::level::level_enum ToSpdlogLevel(Level level);

    std::shared_ptr<spdlog::logger> m_logger;
    Level m_logLevel{Level::Info};
};

}  // namespace KYL

// Global convenience macros
#define LOG_TRACE(...) KYL::Logger::GetInstance().Trace(__VA_ARGS__)
#define LOG_DEBUG(...) KYL::Logger::GetInstance().Debug(__VA_ARGS__)
#define LOG_INFO(...) KYL::Logger::GetInstance().Info(__VA_ARGS__)
#define LOG_WARN(...) KYL::Logger::GetInstance().Warn(__VA_ARGS__)
#define LOG_ERROR(...) KYL::Logger::GetInstance().Error(__VA_ARGS__)
#define LOG_CRITICAL(...) KYL::Logger::GetInstance().Critical(__VA_ARGS__)
