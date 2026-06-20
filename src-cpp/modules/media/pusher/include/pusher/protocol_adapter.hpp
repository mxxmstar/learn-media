#pragma once

#include "defines/media_packet.hpp"
#include "pusher/pusher_config.hpp"

/// @brief 推流协议适配器
/// 隐藏推流实现细节，对 Pusher 只暴露 Connect Send Close 接口
class IProtocolAdapter {
public:
    virtual ~IProtocolAdapter() = default;
    
    /// @brief 连接推流地址
    /// @param config 推流配置参数
    /// @return 连接成功返回true，失败返回false
    virtual bool Connect(const PusherConfig& config) = 0;
    
    /// @brief 发送媒体数据包
    /// @param pkt 媒体数据包
    /// @return 发送成功返回true，失败返回false
    virtual bool Send(const MediaPacket& pkt) = 0;
    
    /// @brief 关闭连接
    /// @return 关闭成功返回true，失败返回false
    virtual bool Close() = 0;
};
