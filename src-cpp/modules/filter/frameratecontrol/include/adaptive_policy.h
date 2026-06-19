#pragma once

#include "i_policy.h"

class AdaptivePolicy : public IFrameRatePolicy {
public:
    struct Config {
        double base_fps = 30.0;  ///< 基础帧率，单位为帧/秒。默认值为30.0。
        double min_fps = 5.0;    ///< 最小帧率，单位为帧/秒。默认值为5.0。
        double max_fps = 60.0;   ///< 最大帧率，单位为帧/秒。默认值为60.0。
        Duration latency_limit{100}; ///< 最大延迟时间，单位为毫秒。默认值为100毫秒。
        double queue_limit = 0.8;  ///< 队列限制，默认值为0.8。
        double decrease = 0.8;     ///< 帧率下降因子，范围[0.05, 0.99]。默认值为0.8。
        double increase = 1.05;   ///< 帧率上升因子，范围[1.0, 2.0]。默认值为1.05。
    };

    /// @brief 构造函数
    /// @param cfg 自适应帧率策略配置
    explicit AdaptivePolicy(Config cfg);

    /// @brief 更新自适应帧率策略
    /// @param feedback 帧率反馈
    void Update(const FrameRateFeedback& feedback) override;

    /// @brief 获取当前目标帧率
    /// @return 当前目标帧率
    double TargetFps() const override;

    void Reset() override;

private:
    /// @brief 计算当前目标帧率
    void Compute();

    Config cfg_;
    FrameRateFeedback feedback_;
    double current_fps_ = 0.0;
};
