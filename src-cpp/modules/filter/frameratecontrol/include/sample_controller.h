#pragma once

#include "i_controller.h"

#include <mutex>

template<typename Frame>
class SampleFrameRateController : public IFrameRateController<Frame> {
public:
    explicit SampleFrameRateController(const FrameRateConfig& config);
    ~SampleFrameRateController() override;

    bool Accept(const Frame& frame, Timestamp pts) override;

    void OnFrameSent(Timestamp ts) override;

    void UpdateLatency(Duration delay) override;

    void SetTargetFps(double fps) override;

    double GetTargetFps() const override;

    FrameRateStats Stats() const override;

    void Reset() override;

private:
    bool shouldAcceptLocked();
    void recordInputLocked(Timestamp ts);
    void refreshDropRatioLocked();

    FrameRateConfig config_;
    FrameRateStats stats_;
    double phase_ = 0.0;
    Timestamp last_input_ts_{};
    Timestamp last_sent_ts_{};
    mutable std::mutex mutex_;
};

extern template class SampleFrameRateController<MediaFrame>;
