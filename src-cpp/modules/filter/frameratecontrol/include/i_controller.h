#pragma once

#include "frame_rate_types.h"
#include "defines/media_frame.hpp"

template<typename Frame>
class IFrameRateController {
public:
    virtual ~IFrameRateController();

    virtual bool Accept(const Frame& frame, Timestamp pts) = 0;

    virtual void OnFrameSent(Timestamp ts) = 0;

    virtual void UpdateLatency(Duration delay) = 0;

    virtual void SetTargetFps(double fps) = 0;

    virtual double GetTargetFps() const = 0;

    virtual FrameRateStats Stats() const = 0;

    virtual void Reset() = 0;
};

extern template class IFrameRateController<MediaFrame>;
