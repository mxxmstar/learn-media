#include "controller_adapter.h"

#include <stdexcept>
#include <utility>

template<typename Frame>
FrameRateAdapter<Frame>::FrameRateAdapter(
    std::shared_ptr<IFrameRateController<Frame>> controller,
    std::shared_ptr<IFrameRatePolicy> policy)
    : controller_(std::move(controller)),
      policy_(std::move(policy)) {
    if (!controller_ || !policy_) {
        throw std::invalid_argument("FrameRateAdapter requires controller and policy");
    }
    syncTargetFps();
}

template<typename Frame>
FrameRateAdapter<Frame>::~FrameRateAdapter() = default;

template<typename Frame>
bool FrameRateAdapter<Frame>::Accept(const Frame& frame, Timestamp pts) {
    syncTargetFps();
    return controller_->Accept(frame, pts);
}

template<typename Frame>
void FrameRateAdapter<Frame>::OnFrameSent(Timestamp ts) {
    controller_->OnFrameSent(ts);
}

template<typename Frame>
void FrameRateAdapter<Frame>::UpdateLatency(Duration delay) {
    controller_->UpdateLatency(delay);
    FrameRateFeedback feedback;
    feedback.latency = delay;
    policy_->Update(feedback);
    syncTargetFps();
}

template<typename Frame>
void FrameRateAdapter<Frame>::SetTargetFps(double fps) {
    controller_->SetTargetFps(fps);
}

template<typename Frame>
double FrameRateAdapter<Frame>::GetTargetFps() const {
    return controller_->GetTargetFps();
}

template<typename Frame>
FrameRateStats FrameRateAdapter<Frame>::Stats() const {
    return controller_->Stats();
}

template<typename Frame>
void FrameRateAdapter<Frame>::Reset() {
    policy_->Reset();
    controller_->Reset();
    syncTargetFps();
}

template<typename Frame>
void FrameRateAdapter<Frame>::Feedback(const FrameRateFeedback& feedback) {
    policy_->Update(feedback);
    controller_->UpdateLatency(feedback.latency);
    syncTargetFps();
}

template<typename Frame>
double FrameRateAdapter<Frame>::TargetFps() const {
    return policy_->TargetFps();
}

template<typename Frame>
void FrameRateAdapter<Frame>::syncTargetFps() {
    const double fps = policy_->TargetFps();
    if (fps > 0.0) {
        controller_->SetTargetFps(fps);
    }
}

template class FrameRateAdapter<MediaFrame>;
