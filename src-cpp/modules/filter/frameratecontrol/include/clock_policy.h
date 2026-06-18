#pragma once

#include "i_policy.h"

class ClockSyncPolicy : public IFrameRatePolicy {
public:
    explicit ClockSyncPolicy(double fps);

    void Update(const FrameRateFeedback& feedback) override;

    double TargetFps() const override;

    void Reset() override;

private:
    double base_fps_ = 0.0;
    double fps_ = 0.0;
    Duration drift_{};
};
