#include "grpc/grpc_client.h"
#include <iostream>
#include <chrono>

// 构造函数：创建自定义通道，设置无限接收消息大小
GrpcClient::GrpcClient(const std::string& target)
    : target_(target) {
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(-1); // 不限接收消息大小

    channel_ = grpc::CreateCustomChannel(target_, grpc::InsecureChannelCredentials(), args);
}

// 阻塞等待通道连接就绪，最多等待 timeout_seconds 秒
bool GrpcClient::WaitForConnected(int timeout_seconds) {
    auto deadline = std::chrono::system_clock::now() +
                    std::chrono::seconds(timeout_seconds);

    return channel_->WaitForConnected(deadline);
}

// 获取通道当前连接状态
grpc_connectivity_state GrpcClient::GetState(bool try_to_connect) {
    return channel_->GetState(try_to_connect);
}

// 创建 ClientContext 并设置截止时间（毫秒），timeout_ms <= 0 则不设截止时间
std::unique_ptr<grpc::ClientContext> GrpcClient::CreateContext(int timeout_ms) {
    auto context = std::make_unique<grpc::ClientContext>();

    if (timeout_ms > 0) {
        auto deadline = std::chrono::system_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        context->set_deadline(deadline);
    }

    return context;
}
