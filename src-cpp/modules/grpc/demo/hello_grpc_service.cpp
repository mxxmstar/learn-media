// Hello 服务的服务端和客户端实现

#include "hello_grpc_service.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace grpc_module {

// ========== HelloServiceImpl 实现 ==========

// 一元 RPC：根据请求中的名字构造问候消息
grpc::Status HelloServiceImpl::SayHello(
    grpc::ServerContext* context,
    const simple_grpc::HelloRequest* request,
    simple_grpc::HelloResponse* response) {

    std::string greeting = "Hello, " + request->name() + "!";

    response->set_message(greeting);
    response->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    std::cout << "[HelloService] SayHello: " << greeting << std::endl;

    return grpc::Status::OK;
}

// 服务端流式 RPC：根据请求中的 count 发送多条问候消息
grpc::Status HelloServiceImpl::SayHelloStream(
    grpc::ServerContext* context,
    const simple_grpc::HelloRequest* request,
    grpc::ServerWriter<simple_grpc::HelloResponse>* writer) {

    std::cout << "[HelloService] SayHelloStream 开始，名称: " << request->name() << std::endl;

    int count = request->count();
    if (count <= 0) {
        count = 5;  // 默认发送 5 条
    }

    for (int i = 0; i < count; ++i) {
        simple_grpc::HelloResponse response;

        response.set_message("Hello " + request->name() + ", message #" + std::to_string(i + 1));
        response.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

        if (!writer->Write(response)) {
            std::cerr << "[HelloService] 流连接断开" << std::endl;
            break;
        }

        // 模拟处理延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "[HelloService] SayHelloStream 结束" << std::endl;

    return grpc::Status::OK;
}

// ========== HelloGrpcServer 实现 ==========

HelloGrpcServer::HelloGrpcServer(const std::string& address)
    : GrpcServer(address) {
}

// 注册 HelloServiceImpl 到 gRPC 服务构建器
void HelloGrpcServer::RegisterServices(grpc::ServerBuilder& builder) {
    builder.RegisterService(&service_impl_);
}

// ========== HelloGrpcClient 实现 ==========

HelloGrpcClient::HelloGrpcClient(const std::string& target)
    : GrpcClient(target) {
}

// 惰性创建 HelloService 的 RPC stub
bool HelloGrpcClient::CreateStub() {
    if (!stub_) {
        stub_ = simple_grpc::HelloService::NewStub(channel_);
    }
    return stub_ != nullptr;
}

// 一元 RPC：发送名称并接收问候响应
bool HelloGrpcClient::SayHello(const std::string& name,
                                simple_grpc::HelloResponse* response,
                                int timeout_ms) {
    if (!CreateStub()) {
        std::cerr << "[HelloGrpcClient] 创建 stub 失败" << std::endl;
        return false;
    }

    simple_grpc::HelloRequest request;
    request.set_name(name);
    request.set_count(1);

    auto context = CreateContext(timeout_ms);

    grpc::Status status = stub_->SayHello(context.get(), request, response);

    if (!status.ok()) {
        std::cerr << "[HelloGrpcClient] SayHello 失败: " << status.error_message() << std::endl;
        return false;
    }

    return true;
}

// 服务端流式 RPC：发送名称并逐条处理流式响应
bool HelloGrpcClient::SayHelloStream(const std::string& name,
                                      int count,
                                      std::function<void(const simple_grpc::HelloResponse&)> callback,
                                      int timeout_ms) {
    if (!CreateStub()) {
        std::cerr << "[HelloGrpcClient] 创建 stub 失败" << std::endl;
        return false;
    }

    simple_grpc::HelloRequest request;
    request.set_name(name);
    request.set_count(count);

    auto context = CreateContext(timeout_ms);

    std::unique_ptr<grpc::ClientReader<simple_grpc::HelloResponse>> reader(
        stub_->SayHelloStream(context.get(), request));

    simple_grpc::HelloResponse response;
    while (reader->Read(&response)) {
        if (callback) {
            callback(response);
        }
    }

    grpc::Status status = reader->Finish();

    if (!status.ok()) {
        std::cerr << "[HelloGrpcClient] SayHelloStream 失败: " << status.error_message() << std::endl;
        return false;
    }

    return true;
}

} // namespace grpc_module
