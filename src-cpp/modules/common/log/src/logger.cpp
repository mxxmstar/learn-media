#include "common/log/logger.h"
#include <filesystem>
#include <spdlog/sinks/daily_file_sink.h>

Logger::Logger(const LoggerConfig& config) : config_(config) {
    std::vector<spdlog::sink_ptr> sinks;
    buildSinks(config_, sinks);

    if (config_.async) {
        if (spdlog::thread_pool() == nullptr) {
            spdlog::init_thread_pool(8192, 1);
        }   
        
        spd_logger_ = std::make_shared<spdlog::async_logger>(
            config_.name,
            sinks.begin(),
            sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );
    } else {
        spd_logger_ = std::make_shared<spdlog::logger>(
            config_.name,
            sinks.begin(),
            sinks.end()
        );
    }
    
    if (config_.is_json) {
        spd_logger_->set_pattern(R"({"time":"%Y-%m-%dT%T.%eZ","module":"%n","level":"%l","msg":"%v"})");
    } else {
        spd_logger_->set_pattern("%Y-%m-%d %T.%e [%t] [%n] [%^%l%$] %v");
    }

    spd_logger_->set_level(config_.level);
    
    if (!config_.async) {
        spd_logger_->flush_on(spdlog::level::trace);
    } else {
        spd_logger_->flush_on(spdlog::level::err);
    }
}

void Logger::SetLevel(spdlog::level::level_enum level) {
    config_.level = level;
    spd_logger_->set_level(level);
}

spdlog::level::level_enum Logger::GetLevel() const {
    return spd_logger_->level();
}

void Logger::SetFormat(const std::string& format) {
    spd_logger_->set_pattern(format);
}

RotationPolicy Logger::GetRotationPolicy() const {
    return config_.policy;
}

std::size_t Logger::GetMaxFileSize() const {
    return config_.max_file_size_mb;
}

std::size_t Logger::GetMaxFiles() const {
    return config_.max_files;
}

std::string Logger::GetLogDir() const {
    return config_.log_dir;
}
void Logger::Flush() {
    spd_logger_->flush();
}

void Logger::buildSinks(const LoggerConfig& config, std::vector<spdlog::sink_ptr>& sinks) {
    if (config.write_to_main_log) {
        static std::shared_ptr<spdlog::sinks::sink> main_sink = nullptr;
        static std::mutex main_sink_mutex;

        std::lock_guard lock(main_sink_mutex);
        if (!main_sink) {
            main_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                config.log_dir + "/server.log", 0, 0);
        }
        sinks.push_back(main_sink);
    }

    if (config.policy != RotationPolicy::NONE) {
        std::filesystem::path file_path = std::filesystem::path(config.log_dir) / (config.name + ".log");
        if (config.policy == RotationPolicy::DAILY) {
            sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                file_path.string(), 0, 0));
        } else if (config.policy == RotationPolicy::FILESIZE) {
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                file_path.string(),
                config.max_file_size_mb * 1024 * 1024,
                config.max_files));
        }
    }

    if (config.write_to_console) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
}
