#pragma once

#include "logger.h"
#include <map>
struct LogConfig;

class LogManager {
public:
    static LogManager& getInstance();

    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    void Init(const std::string& base_dir = "./logs", int async_threads = 1);
    
    void ReloadFromConfig(const LoggerConfig& config);
    
    void ReloadFromConfigs(const std::map<std::string, struct LogConfig>& configs);
    
    bool isInitialized() const { return initialized_; }
    
    void RegisterLogger(const LoggerConfig& config);

    std::shared_ptr<Logger> GetLogger(const std::string& name);        

    void RemoveLogger(const std::string& name);

    bool SetLoggerLevel(const std::string& name, spdlog::level::level_enum level);
    bool GetLoggerLevel(const std::string& name, spdlog::level::level_enum& level);
    bool SetLoggerFormat(const std::string& name, const std::string& format);
    void Shutdown();
    
    void FlushAll();
    
    static LoggerConfig ConvertToLoggerConfig(const struct LogConfig& cfg, 
                                               const std::string& name = "main");

    inline std::shared_ptr<Logger> getMainLogger() { return loggers_["main"]; }

    inline std::shared_ptr<Logger> getErrorLogger() { return loggers_["error"]; }
    
    
private:    
    LogManager() = default;
    ~LogManager() = default;

    std::unordered_map<std::string, std::shared_ptr<Logger>> loggers_;
    mutable std::mutex mutex_;

    bool initialized_ = false;
    std::string log_dir_;

    spdlog::sink_ptr main_sink_;
    spdlog::sink_ptr error_sink_;
    
};

static inline const char* extract_filename(const char* file_path) {
    const char* file_name = strrchr(file_path, '/');
#ifdef _WIN32
    const char* file = strrchr(file_path, '\\');
    if (!file_name || (file && file > file_name)) file_name = file;
#endif
    return file_name ? file_name + 1 : file_path;
}

#define LOG_MAIN LogManager::getInstance().getMainLogger()
#define LOG_ERROR LogManager::getInstance().getErrorLogger()

#define LOG_MAIN_TRACE(...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->trace(__VA_ARGS__); \
} while(0)

#define LOG_MAIN_DEBUG(...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->debug(__VA_ARGS__); \
} while(0)

#define LOG_MAIN_INFO(...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->info(__VA_ARGS__); \
} while(0)

#define LOG_MAIN_WARN(...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->warn(__VA_ARGS__); \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->warn(__VA_ARGS__); \
} while(0)

#define LOG_MAIN_ERROR(...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->error(__VA_ARGS__); \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->error(__VA_ARGS__); \
} while(0)

#define LOG_MAIN_CRITICAL(...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->critical(__VA_ARGS__); \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->critical(__VA_ARGS__); \
} while(0)


#define LOG_ERROR_WARN(...) do { \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->warn(__VA_ARGS__); \
} while(0)

#define LOG_ERROR_ERROR(...) do { \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->error(__VA_ARGS__); \
} while(0)

#define LOG_ERROR_CRITICAL(...) do { \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->critical(__VA_ARGS__); \
} while(0)



#define LOG_MAIN_TRACE_FL(filename, line, ...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->trace("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)

#define LOG_MAIN_DEBUG_FL(filename, line, ...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->debug("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)

#define LOG_MAIN_INFO_FL(filename, line, ...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->info("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)

#define LOG_MAIN_WARN_FL(filename, line, ...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->warn("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->warn("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)

#define LOG_MAIN_ERROR_FL(filename, line, ...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->error("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->error("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)

#define LOG_MAIN_CRITICAL_FL(filename, line, ...) do { \
    LogManager::getInstance().getMainLogger()->GetSpdLogger()->critical("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->critical("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)



#define LOG_ERROR_WARN_FL(filename, line, ...) do { \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->warn("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)

#define LOG_ERROR_ERROR_FL(filename, line, ...) do { \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->error("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)

#define LOG_ERROR_CRITICAL_FL(filename, line, ...) do { \
    LogManager::getInstance().getErrorLogger()->GetSpdLogger()->critical("[{}>{}#{}]{}", extract_filename(filename), __func__, line, spdlog::fmt_lib::format(__VA_ARGS__)); \
} while(0)

#define LOG_MAIN_TRACE_AT(...) LOG_MAIN_TRACE_FL(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_MAIN_DEBUG_AT(...) LOG_MAIN_DEBUG_FL(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_MAIN_INFO_AT(...) LOG_MAIN_INFO_FL(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_MAIN_WARN_AT(...) LOG_MAIN_WARN_FL(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_MAIN_ERROR_AT(...) LOG_MAIN_ERROR_FL(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_MAIN_CRITICAL_AT(...) LOG_MAIN_CRITICAL_FL(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_MAIN_TRACE_AT(...) LOG_MAIN_TRACE_FL(__FILE__, __LINE__, __VA_ARGS__)


#define LOG_ERROR_WARN_AT(...) LOG_ERROR_WARN_FL(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR_ERROR_AT(...) LOG_ERROR_ERROR_FL(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR_CRITICAL_AT(...) LOG_ERROR_CRITICAL_FL(__FILE__, __LINE__, __VA_ARGS__)
