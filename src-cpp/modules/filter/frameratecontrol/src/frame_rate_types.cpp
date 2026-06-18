#include "frame_rate_types.h"
#include "i_controller.h"
#include "i_policy.h"

bool FrameRateConfig::EnableDrop() const {
    return strategy != FrameDropStrategy::None;
}

IFrameRatePolicy::~IFrameRatePolicy() = default;

template<typename Frame>
IFrameRateController<Frame>::~IFrameRateController() = default;

template class IFrameRateController<MediaFrame>;
