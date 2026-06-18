#pragma once

#include "i_controller.h"

#include <atomic>
#include <mutex>

template<typename Frame>
class QueueFrameRateController : public IFrameRateController<Frame> {
public:
    explicit QueueFrameRateController(const FrameRateConfig& config);
    ~QueueFrameRateController() override;

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
    std::atomic<uint32_t> in_flight_{0};
    FrameRateStats stats_;
    Timestamp last_input_ts_{};
    Timestamp last_sent_ts_{};
    mutable std::mutex mutex_;
};

extern template class QueueFrameRateController<MediaFrame>;
