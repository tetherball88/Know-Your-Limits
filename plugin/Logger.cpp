#include "Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace KYL {

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

bool Logger::Initialize(const std::filesystem::path& iniPath, const std::filesystem::path& logPath) {
    try {
        // Create log directory if it doesn't exist
        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
        if (ec) {
            return false;
        }

        // Read log level from INI file
        Level configuredLevel = Level::Info;  // Default level
        if (std::filesystem::exists(iniPath)) {
            std::ifstream iniFile(iniPath);
            if (iniFile.is_open()) {
                std::string line;
                while (std::getline(iniFile, line)) {
                    // Remove whitespace
                    line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());

                    // Skip empty lines and comments
                    if (line.empty() || line[0] == ';' || line[0] == '#') {
                        continue;
                    }

                    // Look for LogLevel= setting
                    if (line.find("LogLevel=") == 0) {
                        std::string levelStr = line.substr(9);  // Skip "LogLevel="
                        configuredLevel = ParseLogLevel(levelStr);
                        break;
                    }
                }
                iniFile.close();
            }
        }

        m_logLevel = configuredLevel;

        // Create spdlog logger
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        m_logger = std::make_shared<spdlog::logger>("KnowYourLimits", std::move(sink));

        // Set spdlog level
        m_logger->set_level(ToSpdlogLevel(m_logLevel));
        m_logger->flush_on(spdlog::level::info);
        m_logger->set_pattern("[%H:%M:%S] [%l] %v");

        spdlog::set_default_logger(m_logger);

        m_logger->info("Logger initialized with level: {}", static_cast<int>(m_logLevel));
        m_logger->info("Log file: {}", logPath.string());
        m_logger->info("Config file: {}", iniPath.string());

        return true;
    } catch (...) {
        // Can't log to file, but we tried
        return false;
    }
}

Logger::Level Logger::ParseLogLevel(const std::string& levelStr) {
    // Convert to lowercase for case-insensitive comparison
    std::string lower = levelStr;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "trace" || lower == "0") {
        return Level::Trace;
    } else if (lower == "debug" || lower == "1") {
        return Level::Debug;
    } else if (lower == "info" || lower == "2") {
        return Level::Info;
    } else if (lower == "warning" || lower == "warn" || lower == "3") {
        return Level::Warning;
    } else if (lower == "error" || lower == "4") {
        return Level::Error;
    } else if (lower == "critical" || lower == "crit" || lower == "5") {
        return Level::Critical;
    } else if (lower == "off" || lower == "6") {
        return Level::Off;
    }

    // Default to Info if unrecognized
    return Level::Info;
}

spdlog::level::level_enum Logger::ToSpdlogLevel(Level level) {
    switch (level) {
        case Level::Trace:
            return spdlog::level::trace;
        case Level::Debug:
            return spdlog::level::debug;
        case Level::Info:
            return spdlog::level::info;
        case Level::Warning:
            return spdlog::level::warn;
        case Level::Error:
            return spdlog::level::err;
        case Level::Critical:
            return spdlog::level::critical;
        case Level::Off:
            return spdlog::level::off;
        default:
            return spdlog::level::info;
    }
}

}  // namespace KYL
