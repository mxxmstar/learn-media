#include "queue_controller.h"

#include <chrono>
#include <cmath>

namespace {

Timestamp now_ts() {
    return std::chrono::steady_clock::now();
}

double normalize_fps(double fps, double fallback) {
    return std::isfinite(fps) && fps > 0.0 ? fps : fallback;
}

double fps_from_delta(Timestamp newer, Timestamp older) {
    if (older == Timestamp{} || newer <= older) {
        return 0.0;
    }

    const double seconds = std::chrono::duration<double>(newer - older).count();
    return seconds > 0.0 ? 1.0 / seconds : 0.0;
}

}  // namespace

template<typename Frame>
QueueFrameRateController<Frame>::QueueFrameRateController(const FrameRateConfig& config)
    : config_(config) {
    config_.target_fps = normalize_fps(config_.target_fps, 30.0);
    if (config_.max_queue == 0) {
        config_.max_queue = 1;
    }
}

template<typename Frame>
QueueFrameRateController<Frame>::~QueueFrameRateController() = default;

template<typename Frame>
bool QueueFrameRateController<Frame>::Accept(const Frame& frame, Timestamp pts) {
    (void)frame;
    const Timestamp ts = pts == Timestamp{} ? now_ts() : pts;

    bool accepted = false;
    uint32_t current = in_flight_.load(std::memory_order_acquire);
    while (current < config_.max_queue) {
        if (in_flight_.compare_exchange_weak(
                current,
                current + 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            accepted = true;
            break;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    recordInputLocked(ts);
    if (accepted) {
        ++stats_.passed;
    } else {
        ++stats_.dropped;
    }
    refreshDropRatioLocked();
    return accepted;
}

template<typename Frame>
void QueueFrameRateController<Frame>::OnFrameSent(Timestamp ts) {
    const Timestamp sent_ts = ts == Timestamp{} ? now_ts() : ts;

    uint32_t current = in_flight_.load(std::memory_order_acquire);
    while (current > 0) {
        if (in_flight_.compare_exchange_weak(
                current,
                current - 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            break;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    stats_.output_fps = fps_from_delta(sent_ts, last_sent_ts_);
    stats_.last_sent = sent_ts;
    last_sent_ts_ = sent_ts;
}

template<typename Frame>
void QueueFrameRateController<Frame>::UpdateLatency(Duration delay) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.avg_delay = delay;
}

template<typename Frame>
void QueueFrameRateController<Frame>::SetTargetFps(double fps) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.target_fps = normalize_fps(fps, config_.target_fps);
}

template<typename Frame>
double QueueFrameRateController<Frame>::GetTargetFps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.target_fps;
}

template<typename Frame>
FrameRateStats QueueFrameRateController<Frame>::Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

template<typename Frame>
void QueueFrameRateController<Frame>::Reset() {
    in_flight_.store(0, std::memory_order_release);

    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = {};
    last_input_ts_ = {};
    last_sent_ts_ = {};
}

template<typename Frame>
void QueueFrameRateController<Frame>::recordInputLocked(Timestamp ts) {
    stats_.input_fps = fps_from_delta(ts, last_input_ts_);
    last_input_ts_ = ts;
}

template<typename Frame>
void QueueFrameRateController<Frame>::refreshDropRatioLocked() {
    const uint64_t total = stats_.passed + stats_.dropped;
    stats_.drop_ratio = total > 0
        ? static_cast<double>(stats_.dropped) / static_cast<double>(total)
        : 0.0;
}

template class QueueFrameRateController<MediaFrame>;
