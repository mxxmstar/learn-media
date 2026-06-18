#include "sample_controller.h"

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
SampleFrameRateController<Frame>::SampleFrameRateController(const FrameRateConfig& config)
    : config_(config) {
    config_.input_fps = normalize_fps(config_.input_fps, 30.0);
    config_.target_fps = normalize_fps(config_.target_fps, config_.input_fps);
}

template<typename Frame>
SampleFrameRateController<Frame>::~SampleFrameRateController() = default;

template<typename Frame>
bool SampleFrameRateController<Frame>::Accept(const Frame& frame, Timestamp pts) {
    (void)frame;
    const Timestamp ts = pts == Timestamp{} ? now_ts() : pts;

    std::lock_guard<std::mutex> lock(mutex_);
    recordInputLocked(ts);

    const bool accepted = shouldAcceptLocked();
    if (accepted) {
        ++stats_.passed;
    } else {
        ++stats_.dropped;
    }

    refreshDropRatioLocked();
    return accepted;
}

template<typename Frame>
void SampleFrameRateController<Frame>::OnFrameSent(Timestamp ts) {
    const Timestamp sent_ts = ts == Timestamp{} ? now_ts() : ts;
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.output_fps = fps_from_delta(sent_ts, last_sent_ts_);
    stats_.last_sent = sent_ts;
    last_sent_ts_ = sent_ts;
}

template<typename Frame>
void SampleFrameRateController<Frame>::UpdateLatency(Duration delay) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.avg_delay = delay;
}

template<typename Frame>
void SampleFrameRateController<Frame>::SetTargetFps(double fps) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.target_fps = normalize_fps(fps, config_.target_fps);
}

template<typename Frame>
double SampleFrameRateController<Frame>::GetTargetFps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.target_fps;
}

template<typename Frame>
FrameRateStats SampleFrameRateController<Frame>::Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

template<typename Frame>
void SampleFrameRateController<Frame>::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = {};
    phase_ = 0.0;
    last_input_ts_ = {};
    last_sent_ts_ = {};
}

template<typename Frame>
bool SampleFrameRateController<Frame>::shouldAcceptLocked() {
    if (config_.target_fps >= config_.input_fps) {
        return true;
    }

    phase_ += config_.target_fps;
    if (phase_ + 1e-9 >= config_.input_fps) {
        phase_ -= config_.input_fps;
        return true;
    }
    return false;
}

template<typename Frame>
void SampleFrameRateController<Frame>::recordInputLocked(Timestamp ts) {
    stats_.input_fps = fps_from_delta(ts, last_input_ts_);
    last_input_ts_ = ts;
}

template<typename Frame>
void SampleFrameRateController<Frame>::refreshDropRatioLocked() {
    const uint64_t total = stats_.passed + stats_.dropped;
    stats_.drop_ratio = total > 0
        ? static_cast<double>(stats_.dropped) / static_cast<double>(total)
        : 0.0;
}

template class SampleFrameRateController<MediaFrame>;
