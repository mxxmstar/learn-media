#include "grpc/grpc_server.h"

#include <iostream>

// 构造函数：保存监听地址
GrpcServer::GrpcServer(const std::string& address)
    : address_(address) {
}

// 析构函数：停止服务器并等待后台线程退出
GrpcServer::~GrpcServer() {
    Stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

// 初始化：构建 gRPC 服务器、绑定端口、注册服务
bool GrpcServer::Initialize() {
    grpc::ServerBuilder builder;

    // 添加非安全监听端口
    builder.AddListeningPort(address_, grpc::InsecureServerCredentials());

    // 调用子类实现的服务注册
    RegisterServices(builder);

    // 构建并启动服务器
    server_ = builder.BuildAndStart();
    if (!server_) {
        std::cerr << "[GrpcServer] 服务器在 " << address_ << " 上启动失败" << std::endl;
        return false;
    }

    std::cout << "[GrpcServer] 服务器监听在 " << address_ << std::endl;
    return true;
}

// 启动服务器（在后台线程中运行）
bool GrpcServer::Start() {
    if (running_) {
        std::cerr << "[GrpcServer] 服务器已在运行" << std::endl;
        return false;
    }

    if (!Initialize()) {
        return false;
    }

    running_ = true;
    server_thread_ = std::thread(&GrpcServer::RunServer, this);

    return true;
}

// 优雅关闭服务器
void GrpcServer::Stop() {
    if (!running_) {
        return;
    }

    std::cout << "[GrpcServer] 正在停止服务器..." << std::endl;
    running_ = false;

    if (server_) {
        server_->Shutdown();
    }
}

// 等待服务器线程结束
void GrpcServer::Wait() {
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    std::cout << "[GrpcServer] 服务器已停止" << std::endl;
}

// 后台线程入口：阻塞等待服务器终止
void GrpcServer::RunServer() {
    if (server_) {
        server_->Wait();
    }
}
