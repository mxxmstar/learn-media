#include "adaptive_policy.h"
#include "clock_policy.h"
#include "compose_policy.h"
#include "fixed_policy.h"
#include "pid_policy.h"
#include "queue_policy.h"
#include "tokenbucket_policy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

/// @brief 确保值为非负
/// @param value 输入值
/// @param fallback 回退值
/// @return 非负值
/// @note 如果输入值为非有限值或负数，返回回退值。
double positive_or(double value, double fallback) {
    return std::isfinite(value) && value > 0.0 ? value : fallback;
}

/// @brief 确保帧率在指定范围内
/// @param fps 输入帧率
/// @param min_fps 最小帧率
/// @param max_fps 最大帧率
/// @return 确保后的帧率
/// @note 如果输入帧率超出指定范围，返回最近的边界值。
double clamp_fps(double fps, double min_fps, double max_fps) {
    min_fps = positive_or(min_fps, 1.0);
    max_fps = positive_or(max_fps, min_fps);
    if (max_fps < min_fps) {
        max_fps = min_fps;
    }
    return std::clamp(positive_or(fps, min_fps), min_fps, max_fps);
}

/// @brief 确保值在0-1范围内
/// @param value 输入值
/// @return 确保后的值
/// @note 如果输入值为非有限值或超出0-1范围，返回0.0。
double usage01(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
}

/// @brief 计算压力比
/// @param value 压力值
/// @param limit 限制值
/// @return 压力比
/// @note 如果输入值为非有限值或超出0-1范围，返回0.0。
double pressure_ratio(double value, double limit) {
    if (!std::isfinite(value) || value <= 0.0 || !std::isfinite(limit) || limit <= 0.0) {
        return 0.0;
    }
    return value / limit;
}

}  // namespace


/////////////////////////固定帧率策略实现////////////////////////////////

FixedPolicy::FixedPolicy(double fps) : fps_(positive_or(fps, 0.0)) {
}

void FixedPolicy::Update(const FrameRateFeedback& feedback) {
    (void)feedback;
}

double FixedPolicy::TargetFps() const {
    return fps_;
}

void FixedPolicy::Reset() {
}


/////////////////////////自适应帧率策略实现////////////////////////////////

AdaptivePolicy::AdaptivePolicy(Config cfg) : cfg_(cfg) {
    cfg_.base_fps = positive_or(cfg_.base_fps, 30.0);
    cfg_.min_fps = positive_or(cfg_.min_fps, 1.0);
    cfg_.max_fps = positive_or(cfg_.max_fps, cfg_.base_fps);
    if (cfg_.max_fps < cfg_.min_fps) {
        cfg_.max_fps = cfg_.min_fps;
    }
    cfg_.base_fps = std::clamp(cfg_.base_fps, cfg_.min_fps, cfg_.max_fps);
    cfg_.queue_limit = positive_or(cfg_.queue_limit, 0.8);
    cfg_.decrease = std::clamp(positive_or(cfg_.decrease, 0.8), 0.05, 0.99);
    cfg_.increase = std::clamp(positive_or(cfg_.increase, 1.05), 1.0, 2.0);
    Reset();
}

void AdaptivePolicy::Update(const FrameRateFeedback& feedback) {
    feedback_ = feedback;
    Compute();
}

double AdaptivePolicy::TargetFps() const {
    return current_fps_;
}

void AdaptivePolicy::Reset() {
    feedback_ = {};
    current_fps_ = cfg_.base_fps;
}

void AdaptivePolicy::Compute() {    
    const double latency_ms = static_cast<double>(feedback_.latency.count());
    const double latency_limit_ms = static_cast<double>(cfg_.latency_limit.count());
    // 计算压力
    const double latency_pressure = pressure_ratio(latency_ms, latency_limit_ms);    
    const double queue_pressure = pressure_ratio(usage01(feedback_.queue_usage), cfg_.queue_limit);
    const double cpu_pressure = pressure_ratio(usage01(feedback_.cpu_usage), 0.90);
    const double gpu_pressure = pressure_ratio(usage01(feedback_.gpu_usage), 0.90);
    const double drop_pressure = pressure_ratio(usage01(feedback_.drop_ratio), 0.05);

    // 取最大压力作为目标帧率调整的依据
    const double pressure = std::max({
        latency_pressure,
        queue_pressure,
        cpu_pressure,
        gpu_pressure,
        drop_pressure
    });

    // 根据压力调整帧率
    if (pressure > 1.0) {
        // 计算压力比，限制在0-4之间
        const double capped_pressure = std::min(pressure, 4.0);
        // 计算帧率调整因子 factor = decrease / pressure
        const double factor = std::clamp(cfg_.decrease / capped_pressure, 0.25, cfg_.decrease);
        current_fps_ = clamp_fps(current_fps_ * factor, cfg_.min_fps, cfg_.max_fps);
        return;
    }

    if (current_fps_ < cfg_.base_fps) {
        current_fps_ = std::min(cfg_.base_fps, current_fps_ * cfg_.increase);
    }
    current_fps_ = clamp_fps(current_fps_, cfg_.min_fps, cfg_.max_fps);
}

///////////////////////////时钟同步策略实现////////////////////////////////

ClockSyncPolicy::ClockSyncPolicy(double fps) : base_fps_(positive_or(fps, 0.0)), fps_(base_fps_) {
}

void ClockSyncPolicy::Update(const FrameRateFeedback& feedback) {
    drift_ = feedback.latency;
    fps_ = base_fps_;
}

double ClockSyncPolicy::TargetFps() const {
    return fps_;
}

void ClockSyncPolicy::Reset() {
    drift_ = Duration{};
    fps_ = base_fps_;
}

///////////////////////////复合策略实现////////////////////////////////

void CompositePolicy::Add(std::shared_ptr<IFrameRatePolicy> policy) {
    if (policy) {
        policies_.push_back(std::move(policy));
    }
}

void CompositePolicy::Update(const FrameRateFeedback& feedback) {
    for (const auto& policy : policies_) {
        policy->Update(feedback);
    }
}

double CompositePolicy::TargetFps() const {
    double selected = std::numeric_limits<double>::infinity();
    for (const auto& policy : policies_) {
        const double fps = policy->TargetFps();
        if (std::isfinite(fps) && fps > 0.0) {
            selected = std::min(selected, fps);
        }
    }
    return std::isfinite(selected) ? selected : 0.0;
}

void CompositePolicy::Reset() {
    for (const auto& policy : policies_) {
        policy->Reset();
    }
}

///////////////////////////PID策略实现////////////////////////////////

PIDPolicy::PIDPolicy(Config cfg) : cfg_(cfg) {
    cfg_.base_fps = positive_or(cfg_.base_fps, 30.0);
    cfg_.min_fps = positive_or(cfg_.min_fps, 1.0);
    cfg_.max_fps = positive_or(cfg_.max_fps, cfg_.base_fps);
    if (cfg_.max_fps < cfg_.min_fps) {
        cfg_.max_fps = cfg_.min_fps;
    }
    cfg_.target_latency_ms = positive_or(cfg_.target_latency_ms, 100.0);
    Reset();
}

void PIDPolicy::Update(const FrameRateFeedback& feedback) {
    const double latency_ms = std::max(0.0, static_cast<double>(feedback.latency.count()));
    const double error = cfg_.target_latency_ms - latency_ms;
    integral_ = std::clamp(
        integral_ + error,
        -cfg_.target_latency_ms * 10.0,
        cfg_.target_latency_ms * 10.0);
    const double derivative = error - previous_error_;
    previous_error_ = error;

    const double correction = cfg_.kp * error + cfg_.ki * integral_ + cfg_.kd * derivative;
    fps_ = clamp_fps(cfg_.base_fps + correction, cfg_.min_fps, cfg_.max_fps);
}

double PIDPolicy::TargetFps() const {
    return fps_;
}

void PIDPolicy::Reset() {
    integral_ = 0.0;
    previous_error_ = 0.0;
    fps_ = clamp_fps(cfg_.base_fps, cfg_.min_fps, cfg_.max_fps);
}

///////////////////////////队列策略实现////////////////////////////////

QueuePolicy::QueuePolicy(double base_fps) : base_fps_(positive_or(base_fps, 0.0)), fps_(base_fps_) {
}

void QueuePolicy::Update(const FrameRateFeedback& feedback) {
    const double usage = usage01(feedback.queue_usage);
    if (usage <= 0.5) {
        fps_ = base_fps_;
        return;
    }

    const double pressure = (usage - 0.5) / 0.5;
    const double scale = 1.0 - 0.8 * std::clamp(pressure, 0.0, 1.0);
    fps_ = std::max(1.0, base_fps_ * scale);
}

double QueuePolicy::TargetFps() const {
    return fps_;
}

void QueuePolicy::Reset() {
    fps_ = base_fps_;
}

///////////////////////////令牌桶策略实现////////////////////////////////

TokenBucketPolicy::TokenBucketPolicy(Config cfg) : cfg_(cfg) {
    cfg_.rate = positive_or(cfg_.rate, 30.0);
    cfg_.burst = positive_or(cfg_.burst, 1.0);
    Reset();
}

void TokenBucketPolicy::Update(const FrameRateFeedback& feedback) {
    tokens_ = std::min(cfg_.burst, tokens_ + 1.0);
    if (feedback.drop_ratio > 0.0) {
        tokens_ = std::max(0.0, tokens_ - usage01(feedback.drop_ratio) * cfg_.burst);
    }
    fps_ = cfg_.rate;
}

double TokenBucketPolicy::TargetFps() const {
    return fps_;
}

void TokenBucketPolicy::Reset() {
    tokens_ = cfg_.burst;
    fps_ = cfg_.rate;
}
