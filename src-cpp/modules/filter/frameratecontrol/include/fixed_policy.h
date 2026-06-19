#pragma once

#include "i_policy.h"

/// @brief 固定帧率策略
/// 该策略将视频流的帧率固定在指定的目标帧率上。
/// @note fps_ 为固定帧率，单位为帧/秒。例如，如果 fps_ 为 30，则目标帧率为 30 帧/秒。
class FixedPolicy : public IFrameRatePolicy {
public:
    explicit FixedPolicy(double fps);
    
    /// @brief 更新策略状态
    /// @param feedback 视频流反馈信息
    /// @note 固定帧率策略不使用反馈信息，直接忽略该方法。
    void Update(const FrameRateFeedback& feedback) override;

    /// @brief 获取目标帧率
    /// @return 目标帧率
    /// @note 目标帧率为固定帧率，单位为帧/秒。
    double TargetFps() const override;

    /// @brief 重置策略状态
    /// 重置后，策略将返回到初始状态，准备处理新的视频流。
    void Reset() override;

private:
    double fps_;
};
