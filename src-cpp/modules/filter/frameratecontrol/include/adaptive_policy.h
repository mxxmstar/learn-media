#pragma once

#include "i_policy.h"

class AdaptivePolicy : public IFrameRatePolicy {
public:
    struct Config {
        double base_fps = 30.0;
        double min_fps = 5.0;
        double max_fps = 60.0;
        Duration latency_limit{100};
        double queue_limit = 0.8;
        double decrease = 0.8;
        double increase = 1.05;
    };

    explicit AdaptivePolicy(Config cfg);

    void Update(const FrameRateFeedback& feedback) override;

    double TargetFps() const override;

    void Reset() override;

private:
    void Compute();

    Config cfg_;
    FrameRateFeedback feedback_;
    double current_fps_ = 0.0;
};
