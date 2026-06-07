#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

enum class RotationPolicy {
    DAILY,
    FILESIZE,
    NONE
};

struct LoggerConfig {
    LoggerConfig() = default;
    LoggerConfig(std::string name, spdlog::level::level_enum level = spdlog::level::info) : name(name), level(level) {}
    std::string name;   
    spdlog::level::level_enum level = spdlog::level::info;
    std::string log_dir = "./logs";
    RotationPolicy policy = RotationPolicy::DAILY;
    std::size_t max_file_size_mb = 100;
    std::size_t max_files = 5;
    bool write_to_main_log = true;
    bool write_to_console = true;
    bool is_json = false;
    bool async = true;
};

class Logger {
public:
    explicit Logger(const LoggerConfig& config);
    ~Logger() = default;

    std::shared_ptr<spdlog::logger> GetSpdLogger() { return spd_logger_; }

    void SetLevel(spdlog::level::level_enum level);

    spdlog::level::level_enum GetLevel() const;

    void SetFormat(const std::string& format);

    RotationPolicy GetRotationPolicy() const;

    std::size_t GetMaxFileSize() const;

    std::size_t GetMaxFiles() const;

    std::string GetLogDir() const;
    void Flush();
private:

    void buildSinks(const LoggerConfig& config, std::vector<spdlog::sink_ptr>& sinks);

    std::shared_ptr<spdlog::logger> spd_logger_;
    LoggerConfig config_;
};
