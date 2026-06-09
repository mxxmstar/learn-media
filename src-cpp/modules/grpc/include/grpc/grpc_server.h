#pragma once

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

/// gRPC 服务器基类。
/// 处理构建器设置、通过纯虚函数 RegisterServices 注册服务，
/// 以及在后台线程中的 start / stop / wait 生命周期管理。
class GrpcServer {
public:
    /// 构造一个监听在 *address*（host:port）的服务器。
    explicit GrpcServer(const std::string& address);
    virtual ~GrpcServer();

    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;

    /// 构建服务器、绑定端口并注册服务。
    virtual bool Initialize();

    /// 在后台线程中启动服务器。
    bool Start();

    /// 发出优雅关闭信号。
    void Stop();

    /// 阻塞直到服务器线程退出。
    void Wait();

    /// 返回服务器是否正在运行。
    bool IsRunning() const { return running_; }

    /// 返回监听地址。
    const std::string& GetAddress() const { return address_; }

protected:
    /// 子类必须重写此方法以注册具体的服务实现。
    virtual void RegisterServices(grpc::ServerBuilder& builder) = 0;

private:
    /// 后台线程入口 —— 调用 server_->Wait()。
    void RunServer();

    std::string address_;
    std::unique_ptr<grpc::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
};
