#pragma once

#include "frame_rate_types.h"
#include "defines/media_frame.hpp"


/// @brief 帧率控制器接口
/// @tparam Frame 输入帧类型
template<typename Frame>
class IFrameRateController {
public:
    virtual ~IFrameRateController();

    /// @brief 接受一帧视频数据
    /// @param frame 输入帧
    /// @param pts 视频时间戳
    /// @return 是否接受该帧
    virtual bool Accept(const Frame& frame, Timestamp pts) = 0;

    /// @brief 通知控制器一帧视频数据已发送
    /// @param ts 视频时间戳
    virtual void OnFrameSent(Timestamp ts) = 0;

    /// @brief 更新延迟时间
    /// @param delay 延迟时间
    virtual void UpdateLatency(Duration delay) = 0;

    /// @brief 设置目标帧率
    /// @param fps 目标帧率
    virtual void SetTargetFps(double fps) = 0;

    /// @brief 获取目标帧率
    /// @return 目标帧率
    virtual double GetTargetFps() const = 0;

    /// @brief 获取统计信息
    /// @return 帧率统计信息
    virtual FrameRateStats Stats() const = 0;

    /// @brief 重置控制器状态
    /// 重置后，控制器将返回到初始状态，准备处理新的视频流  
    virtual void Reset() = 0;
};

extern template class IFrameRateController<MediaFrame>;
