// Hello 服务演示程序 —— 展示 gRPC 一元调用和服务端流式调用的客户端与服务端

#include "hello_grpc_service.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace {

// 全局停止信号，用于 Ctrl+C 优雅退出
std::atomic_bool g_stop_requested{false};

// 信号处理函数：设置停止标志
void HandleSignal(int) {
    g_stop_requested.store(true);
}

// 演示程序命令行选项
struct DemoOptions {
    std::string mode{"client"};          // 运行模式：server / client / both
    std::string address{"0.0.0.0:18082"}; // 服务端监听地址
    std::string target{"127.0.0.1:18082"};// 客户端连接目标
    std::string name{"learn-media"};       // 发送的名称
    int stream_count{0};                   // 流式调用次数（0 表示不调用）
    int timeout_ms{5000};                  // RPC 超时（毫秒）
    bool show_help{false};
};

// 打印使用帮助
void PrintUsage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe << " --mode <server|client|both> [options]\n\n"
        << "Options:\n"
        << "  --address <host:port>      服务端监听地址。默认: 0.0.0.0:18082\n"
        << "  --target <host:port>       客户端连接目标。默认: 127.0.0.1:18082\n"
        << "  --name <text>              客户端发送的名称\n"
        << "  --stream-count <n>         同时调用 SayHelloStream n 次。默认: 0\n"
        << "  --timeout-ms <n>           客户端 RPC 超时。默认: 5000\n";
}

// 读取下一个命令行参数值
bool ReadNextValue(int& index, int argc, char** argv, std::string& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return true;
}

// 解析整型参数
bool ParseInt(const std::string& text, int& out) {
    try {
        std::size_t pos = 0;
        const int value = std::stoi(text, &pos, 10);
        if (pos != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

// 解析所有命令行参数
bool ParseArgs(int argc, char** argv, DemoOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;

        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            return true;
        }
        if (arg == "--mode") {
            if (!ReadNextValue(i, argc, argv, options.mode)) {
                return false;
            }
        } else if (arg == "--address") {
            if (!ReadNextValue(i, argc, argv, options.address)) {
                return false;
            }
        } else if (arg == "--target") {
            if (!ReadNextValue(i, argc, argv, options.target)) {
                return false;
            }
        } else if (arg == "--name") {
            if (!ReadNextValue(i, argc, argv, options.name)) {
                return false;
            }
        } else if (arg == "--stream-count") {
            if (!ReadNextValue(i, argc, argv, value) ||
                !ParseInt(value, options.stream_count) ||
                options.stream_count < 0) {
                return false;
            }
        } else if (arg == "--timeout-ms") {
            if (!ReadNextValue(i, argc, argv, value) ||
                !ParseInt(value, options.timeout_ms) ||
                options.timeout_ms <= 0) {
                return false;
            }
        } else {
            return false;
        }
    }

    return options.mode == "server" || options.mode == "client" || options.mode == "both";
}

// 运行客户端：调用 SayHello 和可选的 SayHelloStream
bool RunClient(const DemoOptions& options) {
    grpc_module::HelloGrpcClient client(options.target);

    simple_grpc::HelloResponse response;
    if (!client.SayHello(options.name, &response, options.timeout_ms)) {
        return false;
    }

    std::cout << "SayHello 响应: " << response.message()
              << " 时间戳=" << response.timestamp() << "\n";

    // 如果指定了流式计数，调用服务端流式 RPC
    if (options.stream_count > 0) {
        return client.SayHelloStream(
            options.name,
            options.stream_count,
            [](const simple_grpc::HelloResponse& item) {
                std::cout << "流式响应: " << item.message()
                          << " 时间戳=" << item.timestamp() << "\n";
            },
            options.timeout_ms);
    }

    return true;
}

// 运行服务端：启动 gRPC 服务器并等待停止信号
int RunServer(const DemoOptions& options) {
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    grpc_module::HelloGrpcServer server(options.address);
    if (!server.Start()) {
        return 2;
    }

    std::cout << "hello 服务运行中，按 Ctrl+C 停止\n";
    while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.Stop();
    server.Wait();
    return 0;
}

} // namespace

// 程序入口
int main(int argc, char** argv) {
    DemoOptions options;
    if (!ParseArgs(argc, argv, options)) {
        PrintUsage(argv[0]);
        return 1;
    }
    if (options.show_help) {
        PrintUsage(argv[0]);
        return 0;
    }

    if (options.mode == "server") {
        return RunServer(options);
    }

    if (options.mode == "both") {
        // 同时启动服务端和客户端
        grpc_module::HelloGrpcServer server(options.address);
        if (!server.Start()) {
            return 2;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const bool ok = RunClient(options);
        server.Stop();
        server.Wait();
        return ok ? 0 : 3;
    }

    // 仅运行客户端
    return RunClient(options) ? 0 : 3;
}
