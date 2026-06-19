#pragma once

#include <chrono>
#include <cstdint>

using Timestamp = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;

/// @brief 帧率控制策略
enum class FrameDropStrategy {
    None,
    Sample,                     ///< 采样丢帧
    QueueOverflow,              ///< 队列溢出丢帧
    Adaptive,                   ///< 自适应丢帧
    Pace                        ///< 定时丢帧
};

/// @brief 帧率反馈信息
struct FrameRateFeedback {
    Duration latency{};          ///< 延迟时间
    double queue_usage = 0.0;    ///< 队列占用率
    double drop_ratio = 0.0;     ///< 丢帧比例
    double cpu_usage = 0.0;    ///< CPU 占用率
    double gpu_usage = 0.0;    ///< GPU 占用率
};

/// @brief 帧率统计信息
struct FrameRateStats {
    double input_fps = 0.0;      ///< 输入帧率
    double output_fps = 0.0;     ///< 输出帧率
    double drop_ratio = 0.0;     ///< 丢帧比例
    uint64_t dropped = 0;         ///< 丢帧数
    uint64_t passed = 0;          ///< 通过数
    Duration avg_delay{};         ///< 平均延迟时间
    Timestamp last_sent{};        ///< 最后发送时间
};

/// @brief 帧率配置信息
struct FrameRateConfig {
    FrameDropStrategy strategy = FrameDropStrategy::None; ///< 丢帧策略
    double input_fps = 30.0;                            ///< 输入帧率
    double target_fps = 30.0;                           ///< 目标帧率
    double current_fps = 0.0;                           ///< 当前帧率
    uint32_t max_queue = 30;                             ///< 最大队列大小
    double latency_threshold_ms = 100.0;               ///< 延迟阈值（毫秒）
    double decrease_factor = 0.8;                      ///< 降低帧率因子

    bool EnableDrop() const;     ///< 是否启用丢帧
};
