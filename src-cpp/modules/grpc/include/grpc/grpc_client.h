#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

/// gRPC 客户端基类。
/// 管理一条非安全通道，提供无限消息大小、连接就绪检测和基于截止时间的上下文创建。
class GrpcClient {
public:
    /// 构造连接到 *target*（host:port）的客户端。
    explicit GrpcClient(const std::string& target);
    virtual ~GrpcClient() = default;

    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

    /// 返回底层 gRPC 通道。
    std::shared_ptr<grpc::Channel> GetChannel() const { return channel_; }

    /// 返回目标地址字符串。
    const std::string& GetTarget() const { return target_; }

    /// 阻塞直到通道连接就绪或 *timeout_seconds* 秒超时。
    bool WaitForConnected(int timeout_seconds = 5);

    /// 返回当前通道的连接状态。
    grpc_connectivity_state GetState(bool try_to_connect = false);

protected:
    /// 创建一个设置了截止时间（毫秒）的 ClientContext，0 表示无截止时间。
    static std::unique_ptr<grpc::ClientContext> CreateContext(int timeout_ms = 5000);

    std::string target_;
    std::shared_ptr<grpc::Channel> channel_;
};
