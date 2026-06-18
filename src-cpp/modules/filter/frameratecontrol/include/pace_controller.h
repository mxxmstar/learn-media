#pragma once

#include "i_controller.h"

#include <mutex>

template<typename Frame>
class PaceFrameRateController : public IFrameRateController<Frame> {
public:
    explicit PaceFrameRateController(const FrameRateConfig& config);
    ~PaceFrameRateController() override;

    bool Accept(const Frame& frame, Timestamp pts) override;

    void OnFrameSent(Timestamp ts) override;

    void UpdateLatency(Duration delay) override;

    void SetTargetFps(double fps) override;

    double GetTargetFps() const override;

    FrameRateStats Stats() const override;

    void Reset() override;

private:
    void recordInputLocked(Timestamp ts);
    void refreshDropRatioLocked();

    FrameRateConfig config_;
    FrameRateStats stats_;
    Timestamp next_allowed_ts_{};
    Timestamp last_input_ts_{};
    Timestamp last_sent_ts_{};
    mutable std::mutex mutex_;
};

extern template class PaceFrameRateController<MediaFrame>;
