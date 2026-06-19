#pragma once

#include "i_policy.h"
/// @brief 时钟同步策略，主要同步 inference 和 capture 速率不同导致的帧率差异
/// 丢弃采集端过快的帧来对齐 inference 速率
class ClockSyncPolicy : public IFrameRatePolicy {
public:
    /// @brief 构造函数，设置目标帧率    
    explicit ClockSyncPolicy(double fps);

    /// @brief 更新策略，根据反馈调整输出帧率
    /// @param feedback 输入帧率反馈
    void Update(const FrameRateFeedback& feedback) override;

    /// @brief 获取当前目标帧率
    /// @return 当前目标帧率
    double TargetFps() const override;

    /// @brief 重置策略，将当前帧率重置为初始值
    void Reset() override;

private:
    double base_fps_ = 0.0;  ///< 基础帧率
    double fps_ = 0.0;  ///< 当前输出帧率
    Duration drift_{};  ///< 帧率差异，用于调整输出帧率
};
