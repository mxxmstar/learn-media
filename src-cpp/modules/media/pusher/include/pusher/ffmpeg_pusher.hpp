#pragma once

#include <memory>

#include "pusher/i_pusher.hpp"
#include "pusher/protocol_adapter.hpp"

class FFmpegPusher : public IPusher {
public:
    explicit FFmpegPusher(PusherConfig config);
    FFmpegPusher(PusherConfig config, std::unique_ptr<IProtocolAdapter> adapter);
    ~FFmpegPusher() override;

    /// @brief 配置推流参数
    bool Connect() override;
    
    /// @brief 推流发送
    bool Send(MediaPacket pkt) override;
    
    /// @brief 关闭推流
    bool Close() override;
    
private:
    PusherConfig config_;   ///< 推流配置参数
    std::unique_ptr<IProtocolAdapter> adapter_; ///< 协议适配器
    bool connected_{false}; ///< 是否已经连接
};
