#pragma once

#include "frame_rate_types.h"

class IFrameRatePolicy {
public:
    virtual ~IFrameRatePolicy();

    virtual void Update(const FrameRateFeedback& feedback) = 0;

    virtual double TargetFps() const = 0;

    virtual void Reset() = 0;
};
