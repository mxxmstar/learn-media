#include "pace_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

Timestamp now_ts() {
    return std::chrono::steady_clock::now();
}

double normalize_fps(double fps, double fallback) {
    return std::isfinite(fps) && fps > 0.0 ? fps : fallback;
}

Timestamp::duration interval_for_fps(double fps) {
    fps = normalize_fps(fps, 30.0);
    auto interval = std::chrono::duration_cast<Timestamp::duration>(
        std::chrono::duration<double>(1.0 / fps));
    if (interval <= Timestamp::duration::zero()) {
        interval = std::chrono::milliseconds(1);
    }
    return interval;
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
PaceFrameRateController<Frame>::PaceFrameRateController(const FrameRateConfig& config)
    : config_(config) {
    config_.target_fps = normalize_fps(config_.target_fps, 30.0);
}

template<typename Frame>
PaceFrameRateController<Frame>::~PaceFrameRateController() = default;

template<typename Frame>
bool PaceFrameRateController<Frame>::Accept(const Frame& frame, Timestamp pts) {
    (void)frame;
    const Timestamp ts = pts == Timestamp{} ? now_ts() : pts;

    std::lock_guard<std::mutex> lock(mutex_);
    recordInputLocked(ts);

    const auto interval = interval_for_fps(config_.target_fps);
    const bool accepted = next_allowed_ts_ == Timestamp{} || ts >= next_allowed_ts_;
    if (accepted) {
        ++stats_.passed;
        if (next_allowed_ts_ == Timestamp{} || ts > next_allowed_ts_) {
            next_allowed_ts_ = ts + interval;
        } else {
            next_allowed_ts_ += interval;
        }
    } else {
        ++stats_.dropped;
    }

    refreshDropRatioLocked();
    return accepted;
}

template<typename Frame>
void PaceFrameRateController<Frame>::OnFrameSent(Timestamp ts) {
    const Timestamp sent_ts = ts == Timestamp{} ? now_ts() : ts;
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.output_fps = fps_from_delta(sent_ts, last_sent_ts_);
    stats_.last_sent = sent_ts;
    last_sent_ts_ = sent_ts;
}

template<typename Frame>
void PaceFrameRateController<Frame>::UpdateLatency(Duration delay) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.avg_delay = delay;
}

template<typename Frame>
void PaceFrameRateController<Frame>::SetTargetFps(double fps) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.target_fps = normalize_fps(fps, config_.target_fps);
}

template<typename Frame>
double PaceFrameRateController<Frame>::GetTargetFps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.target_fps;
}

template<typename Frame>
FrameRateStats PaceFrameRateController<Frame>::Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

template<typename Frame>
void PaceFrameRateController<Frame>::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = {};
    next_allowed_ts_ = {};
    last_input_ts_ = {};
    last_sent_ts_ = {};
}

template<typename Frame>
void PaceFrameRateController<Frame>::recordInputLocked(Timestamp ts) {
    stats_.input_fps = fps_from_delta(ts, last_input_ts_);
    last_input_ts_ = ts;
}

template<typename Frame>
void PaceFrameRateController<Frame>::refreshDropRatioLocked() {
    const uint64_t total = stats_.passed + stats_.dropped;
    stats_.drop_ratio = total > 0
        ? static_cast<double>(stats_.dropped) / static_cast<double>(total)
        : 0.0;
}

template class PaceFrameRateController<MediaFrame>;
