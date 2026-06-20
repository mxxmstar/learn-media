#pragma once

#include "pusher/protocol_adapter.hpp"

struct AVFormatContext;
struct AVPacket;

/// @brief FFmpeg 协议适配器
class FFmpegProtocolAdapter : public IProtocolAdapter {
public:
    FFmpegProtocolAdapter() = default;
    ~FFmpegProtocolAdapter() override;

    FFmpegProtocolAdapter(const FFmpegProtocolAdapter&) = delete;
    FFmpegProtocolAdapter& operator=(const FFmpegProtocolAdapter&) = delete;

    bool Connect(const PusherConfig& config) override;
    bool Send(const MediaPacket& pkt) override;
    bool Close() override;

    static int MapCodecType(CodecType type);

private:
    /// @brief 创建 AVStream，配置编码参数
    bool BuildStream();

    /// @brief 将 MediaPacket 转换成 AVPacket
    /// @param pkt 输入的媒体数据包
    /// @param out 输出的AVPacket
    /// @return 转换是否成功
    bool BuildPacket(const MediaPacket& pkt, AVPacket* out) const;

    /// @brief 释放 AVFormatContext 资源
    /// @param write_trailer 是否先写文件尾
    /// @return 是否成功释放资源
    bool FreeContext(bool write_trailer);

    PusherConfig config_;
    AVFormatContext* fmt_ctx_{nullptr};
    int stream_index_{-1};
    bool header_written_{false};
};
