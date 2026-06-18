#pragma once

#include "i_policy.h"

class FixedPolicy : public IFrameRatePolicy {
public:
    explicit FixedPolicy(double fps);

    void Update(const FrameRateFeedback& feedback) override;

    double TargetFps() const override;

    void Reset() override;

private:
    double fps_;
};
