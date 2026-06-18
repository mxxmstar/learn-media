#pragma once

#include <chrono>
#include <cstdint>

using Timestamp = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;

enum class FrameDropStrategy {
    None,
    Sample,
    QueueOverflow,
    Adaptive,
    Pace
};

struct FrameRateFeedback {
    Duration latency{};
    double queue_usage = 0.0;
    double drop_ratio = 0.0;
    double cpu_usage = 0.0;
    double gpu_usage = 0.0;
};

struct FrameRateStats {
    double input_fps = 0.0;
    double output_fps = 0.0;
    double drop_ratio = 0.0;
    uint64_t dropped = 0;
    uint64_t passed = 0;
    Duration avg_delay{};
    Timestamp last_sent{};
};

struct FrameRateConfig {
    FrameDropStrategy strategy = FrameDropStrategy::None;
    double input_fps = 30.0;
    double target_fps = 30.0;
    double current_fps = 0.0;
    uint32_t max_queue = 30;
    double latency_threshold_ms = 100.0;
    double decrease_factor = 0.8;

    bool EnableDrop() const;
};
