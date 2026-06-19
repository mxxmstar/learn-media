#pragma once

#include "i_policy.h"

class PIDPolicy : public IFrameRatePolicy {
public:
    struct Config {
        double kp = 0.1;    ///< 增益系数
        double ki = 0.0;    ///< 积分系数
        double kd = 0.0;    ///< 微分系数
        double target_latency_ms = 100.0;  ///< 目标延迟时间，单位毫秒
        double base_fps = 30.0;
        double min_fps = 1.0;
        double max_fps = 60.0;
    };

    explicit PIDPolicy(Config cfg);

    void Update(const FrameRateFeedback& feedback) override;

    double TargetFps() const override;

    void Reset() override;

private:
    Config cfg_;
    double integral_ = 0.0;
    double previous_error_ = 0.0;
    double fps_ = 0.0;
};
