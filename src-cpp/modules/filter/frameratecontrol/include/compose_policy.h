#pragma once

#include "i_policy.h"

#include <memory>
#include <vector>

class CompositePolicy : public IFrameRatePolicy {
public:
    void Add(std::shared_ptr<IFrameRatePolicy> policy);

    void Update(const FrameRateFeedback& feedback) override;

    double TargetFps() const override;

    void Reset() override;

private:
    std::vector<std::shared_ptr<IFrameRatePolicy>> policies_;
};
