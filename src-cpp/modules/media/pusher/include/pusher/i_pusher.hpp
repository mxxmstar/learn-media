#pragma once

#include <memory>

#include "defines/media_packet.hpp"
#include "pusher/pusher_config.hpp"

class IPusher {
public:
    virtual ~IPusher() = default;

    /// @brief 配置推流参数
    virtual bool Connect() = 0;
    
    /// @brief 推流发送
    /// @param pkt 视频流数据包
    virtual bool Send(MediaPacket pkt) = 0;
    
    /// @brief 关闭推流
    virtual bool Close() = 0;

    virtual bool Disconnect() { return Close(); }

    static std::unique_ptr<IPusher> Create(PusherConfig config);
};
