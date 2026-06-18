#pragma once

#include "i_controller.h"
#include "i_policy.h"

#include <memory>

template<typename Frame>
class FrameRateAdapter : public IFrameRateController<Frame> {
public:
    FrameRateAdapter(
        std::shared_ptr<IFrameRateController<Frame>> controller,
        std::shared_ptr<IFrameRatePolicy> policy);
    ~FrameRateAdapter() override;

    bool Accept(const Frame& frame, Timestamp pts) override;

    void OnFrameSent(Timestamp ts) override;

    void UpdateLatency(Duration delay) override;

    void SetTargetFps(double fps) override;

    double GetTargetFps() const override;

    FrameRateStats Stats() const override;

    void Reset() override;

    void Feedback(const FrameRateFeedback& feedback);

    double TargetFps() const;

private:
    void syncTargetFps();

    std::shared_ptr<IFrameRateController<Frame>> controller_;
    std::shared_ptr<IFrameRatePolicy> policy_;
};

extern template class FrameRateAdapter<MediaFrame>;
