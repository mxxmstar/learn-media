#include "grpc/frame_ingest_client.h"

#include <iostream>

namespace grpc_module {

// 构造函数：调用基类 GrpcClient 连接目标服务器
FrameIngestClient::FrameIngestClient(const std::string& target)
    : GrpcClient(target) {
}

// 惰性创建 VideoFrameIngest 的 RPC stub
bool FrameIngestClient::EnsureStub() {
    if (!stub_) {
        stub_ = learn_media::grpcdemo::VideoFrameIngest::NewStub(GetChannel());
    }
    return stub_ != nullptr;
}

// 发送单帧到服务器，返回 true 表示服务器确认接收成功
bool FrameIngestClient::SendFrame(const FrameSendOptions& options,
                                  learn_media::grpcdemo::FrameAck* ack,
                                  int timeout_ms) {
    if (!ack) {
        std::cerr << "[FrameIngestClient] ack 指针为空\n";
        return false;
    }
    if (!EnsureStub()) {
        std::cerr << "[FrameIngestClient] 创建 stub 失败\n";
        return false;
    }

    // 构建 FrameRequest 消息
    learn_media::grpcdemo::FrameRequest request;
    request.set_video_id(options.video_id);
    request.set_frame_index(options.frame_index);
    request.set_timestamp_ms(options.timestamp_ms);
    request.set_image_format(options.image_format);
    request.set_image_data(
        reinterpret_cast<const char*>(options.image_data.data()),
        options.image_data.size());
    request.set_width(options.width);
    request.set_height(options.height);
    request.set_prompt(options.prompt);
    request.set_schema_version(options.schema_version);
    // 复制元数据
    for (const auto& [key, value] : options.metadata) {
        (*request.mutable_metadata())[key] = value;
    }

    // 发送 RPC 请求
    auto context = CreateContext(timeout_ms);
    const grpc::Status status = stub_->SendFrame(context.get(), request, ack);
    if (!status.ok()) {
        std::cerr << "[FrameIngestClient] SendFrame 失败: code="
                  << status.error_code()
                  << " message=" << status.error_message() << "\n";
        return false;
    }

    // 返回服务器的确认状态
    return ack->ok();
}

} // namespace grpc_module
