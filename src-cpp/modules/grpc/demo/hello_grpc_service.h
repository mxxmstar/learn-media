#pragma once

#include "grpc/grpc_server.h"
#include "grpc/grpc_client.h"
#include "hello.grpc.pb.h"

#include <functional>
#include <memory>
#include <string>

namespace grpc_module {

/// Hello 服务实现类（服务端）
class HelloServiceImpl final : public simple_grpc::HelloService::Service {
public:
    /// 一元 RPC：处理 SayHello 请求，返回问候消息
    grpc::Status SayHello(
        grpc::ServerContext* context,
        const simple_grpc::HelloRequest* request,
        simple_grpc::HelloResponse* response) override;

    /// 服务端流式 RPC：处理 SayHelloStream 请求，发送多条问候消息
    grpc::Status SayHelloStream(
        grpc::ServerContext* context,
        const simple_grpc::HelloRequest* request,
        grpc::ServerWriter<simple_grpc::HelloResponse>* writer) override;
};

/// Hello gRPC 服务器（包装 GrpcServer）
class HelloGrpcServer : public GrpcServer {
public:
    explicit HelloGrpcServer(const std::string& address = "0.0.0.0:18082");

protected:
    /// 注册 HelloServiceImpl 到服务器构建器
    void RegisterServices(grpc::ServerBuilder& builder) override;

private:
    HelloServiceImpl service_impl_;
};

/// Hello gRPC 客户端
class HelloGrpcClient : public GrpcClient {
public:
    explicit HelloGrpcClient(const std::string& target = "127.0.0.1:18082");

    /// 一元 RPC：发送名称并获取问候响应
    bool SayHello(const std::string& name,
                  simple_grpc::HelloResponse* response,
                  int timeout_ms = 5000);

    /// 服务端流式 RPC：发送名称并通过回调处理多条流式响应
    bool SayHelloStream(const std::string& name,
                        int count,
                        std::function<void(const simple_grpc::HelloResponse&)> callback,
                        int timeout_ms = 10000);

private:
    std::unique_ptr<simple_grpc::HelloService::Stub> stub_;
    bool CreateStub();
};

} // namespace grpc_module
