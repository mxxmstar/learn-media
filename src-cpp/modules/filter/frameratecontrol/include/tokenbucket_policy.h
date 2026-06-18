#pragma once

#include "i_policy.h"

class TokenBucketPolicy : public IFrameRatePolicy {
public:
    struct Config {
        double rate = 30.0;
        double burst = 1.0;
    };

    explicit TokenBucketPolicy(Config cfg);

    void Update(const FrameRateFeedback& feedback) override;

    double TargetFps() const override;

    void Reset() override;

private:
    Config cfg_;
    double tokens_ = 0.0;
    double fps_ = 0.0;
};
