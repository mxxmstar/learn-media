#include "common/log/logmanager.h"
#include "common/config/config.h"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

LogManager& LogManager::getInstance() {
    static LogManager instance;
    return instance;
}

void LogManager::Init(const std::string& base_dir, int async_threads) {
    if (initialized_) {
        return;
    }

    spdlog::init_thread_pool(8192, async_threads);

    log_dir_ = base_dir;
    if (log_dir_.back() != '/') {
        log_dir_ += '/';
    }

    auto main_config = LoggerConfig("main");    
    auto error_config = LoggerConfig("error", spdlog::level::err);
#if DEBUG_MODE
    main_config.level = spdlog::level::trace;
    main_config.async = false;
    error_config.async = false;
#else    
    main_config.level = spdlog::level::info;
    main_config.async = true;
    error_config.async = true;    
#endif
    loggers_["main"] = std::make_shared<Logger>(main_config);
    loggers_["error"] = std::make_shared<Logger>(error_config);
    
    initialized_ = true;
}

void LogManager::ReloadFromConfig(const LoggerConfig& config) {
    if (!initialized_) {
        Init();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loggers_.find(config.name);
    if (it == loggers_.end()) {
        auto new_logger = std::make_shared<Logger>(config);
        loggers_[config.name] = new_logger;
        LOG_MAIN_INFO_AT("Created new logger: {}", config.name);
        return;
    }
    
    bool needs_rebuild = false;
    
    if (it->second->GetLevel() != config.level) {
        it->second->SetLevel(config.level);
        LOG_MAIN_INFO_AT("Logger '{}' level updated: {} -> {}", 
            config.name, 
            spdlog::level::to_string_view(it->second->GetLevel()),
            spdlog::level::to_string_view(config.level));
    }
    
    if (it->second->GetRotationPolicy() != config.policy ||
        it->second->GetMaxFileSize() != config.max_file_size_mb ||
        it->second->GetMaxFiles() != config.max_files ||
        it->second->GetLogDir() != config.log_dir) {
        needs_rebuild = true;
    }

    if (needs_rebuild) {
        LOG_MAIN_WARN_AT("Logger '{}' requires rebuild due to configuration change", 
            config.name);
        it->second->Flush();
        
        auto new_logger = std::make_shared<Logger>(config);
        loggers_[config.name] = new_logger;
        
        LOG_MAIN_INFO_AT("Logger '{}' rebuilt with new configuration", config.name);
        return;
    }
    
    LOG_MAIN_INFO_AT("Logger '{}' configuration reloaded", config.name);
}

void LogManager::ReloadFromConfigs(const std::map<std::string, LogConfig>& configs) {
    if (!initialized_) {
        Init();
    }
    
    LOG_MAIN_INFO_AT("[LogManager] Reloading {} logger configurations...", configs.size());
    
    for (const auto& [name, log_cfg] : configs) {
        LoggerConfig logger_cfg = ConvertToLoggerConfig(log_cfg, name);
        ReloadFromConfig(logger_cfg);
    }
    
    LOG_MAIN_INFO_AT("[LogManager] All logger configurations reloaded successfully");
}

void LogManager::RegisterLogger(const LoggerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(config.name);
    if (it != loggers_.end()) {
        return;
    }

    if (!initialized_) {
        Init();
    }

    loggers_[config.name] = std::make_shared<Logger>(config);
}

std::shared_ptr<Logger> LogManager::GetLogger(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        return it->second;
    }
    return nullptr;
}

void LogManager::RemoveLogger(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        loggers_.erase(it);
    }
}

bool LogManager::SetLoggerLevel(const std::string& name, spdlog::level::level_enum level) {
    auto logger = GetLogger(name);
    if (logger) {
        logger->SetLevel(level);
        return true;
    }
    return false;
}
bool LogManager::GetLoggerLevel(const std::string& name, spdlog::level::level_enum& level) {
    auto logger = GetLogger(name);
    if (logger) {
        level = logger->GetLevel();
        return true;
    }
    return false;
}

bool LogManager::SetLoggerFormat(const std::string& name, const std::string& format) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        it->second->SetFormat(format);
        return true;
    }
    return false;
}

void LogManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& logger : loggers_) {
        logger.second->Flush();
    }
}

void LogManager::FlushAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [name, logger] : loggers_) {
        if (logger) {
            auto spd_logger = logger->GetSpdLogger();
            if (spd_logger) {
                spd_logger->flush();
            }
        }
    }
    
    spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) {
        l->flush();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

LoggerConfig LogManager::ConvertToLoggerConfig(const LogConfig& cfg, const std::string& name) {
    LoggerConfig logger_cfg;
    logger_cfg.name = name;
    
    if (cfg.level == "trace") logger_cfg.level = spdlog::level::trace;
    else if (cfg.level == "debug") logger_cfg.level = spdlog::level::debug;
    else if (cfg.level == "info") logger_cfg.level = spdlog::level::info;
    else if (cfg.level == "warn" || cfg.level == "warning") logger_cfg.level = spdlog::level::warn;
    else if (cfg.level == "error") logger_cfg.level = spdlog::level::err;
    else if (cfg.level == "critical") logger_cfg.level = spdlog::level::critical;
    else logger_cfg.level = spdlog::level::info;
    
    logger_cfg.log_dir = cfg.dir;
    logger_cfg.write_to_console = cfg.console;
    logger_cfg.is_json = cfg.json_format;
    logger_cfg.async = cfg.async;
    
    if (cfg.rotation == "daily") {
        logger_cfg.policy = RotationPolicy::DAILY;
    } else if (cfg.rotation == "filesize") {
        logger_cfg.policy = RotationPolicy::FILESIZE;
    } else {
        logger_cfg.policy = RotationPolicy::NONE;
    }
    
    logger_cfg.max_file_size_mb = cfg.max_file_size_mb;
    logger_cfg.max_files = cfg.max_files;
    
    return logger_cfg;
}
