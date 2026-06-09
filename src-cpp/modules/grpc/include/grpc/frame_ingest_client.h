#pragma once

#include "grpc/grpc_client.h"
#include "video_frame.grpc.pb.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace grpc_module {

/// 客户端使用的当前 schema 版本号。
constexpr std::uint32_t kVideoFrameSchemaVersion = 1;

/// 封装构建 FrameRequest 所需的所有字段。
struct FrameSendOptions {
    std::string video_id{"demo-video"};
    std::uint64_t frame_index{0};
    std::int64_t timestamp_ms{0};
    std::string image_format{"png"};
    std::vector<std::uint8_t> image_data;
    int width{0};
    int height{0};
    std::string prompt{"Describe this frame."};
    std::map<std::string, std::string> metadata;
    std::uint32_t schema_version{kVideoFrameSchemaVersion};
};

/// VideoFrameIngest 服务的 gRPC 客户端。
class FrameIngestClient final : public GrpcClient {
public:
    /// 连接到 *target* 地址的 VideoFrameIngest 服务。
    explicit FrameIngestClient(const std::string& target);

    /// 发送单帧；服务器返回 ok() 时返回 true。
    bool SendFrame(const FrameSendOptions& options,
                   learn_media::grpcdemo::FrameAck* ack,
                   int timeout_ms = 3000);

private:
    /// 惰性创建 RPC stub。
    bool EnsureStub();

    std::unique_ptr<learn_media::grpcdemo::VideoFrameIngest::Stub> stub_;
};

} // namespace grpc_module
