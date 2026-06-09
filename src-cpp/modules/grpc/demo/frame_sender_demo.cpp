// 帧发送演示程序 —— 以固定速率向 gRPC 服务器发送视频帧

#include "grpc/frame_ingest_client.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using grpc_module::FrameIngestClient;
using grpc_module::FrameSendOptions;
using learn_media::grpcdemo::FrameAck;

// 全局停止信号，用于 Ctrl+C 优雅退出
std::atomic_bool g_stop_requested{false};

// 信号处理函数：设置停止标志
void HandleSignal(int) {
    g_stop_requested.store(true);
}

// 演示程序的命令行选项
struct DemoOptions {
    std::string server{"127.0.0.1:18080"};
    std::string image_path;
    std::string video_id{"demo-video"};
    std::string prompt{"Describe this frame."};
    std::string image_format;
    double fps{1.0};
    int count{5};
    int timeout_ms{3000};
    int width{0};
    int height{0};
    bool show_help{false};
};

// 打印使用帮助
void PrintUsage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe << " [options]\n\n"
        << "Options:\n"
        << "  --server <host:port>   gRPC 服务器地址。默认: 127.0.0.1:18080\n"
        << "  --image <path>         要发送的图片文件。默认: 内建 1x1 PNG\n"
        << "  --format <name>        图片格式，如 png 或 jpeg。默认: 从 --image 推断\n"
        << "  --video-id <id>        逻辑视频 ID。默认: demo-video\n"
        << "  --prompt <text>        每帧携带的文本提示\n"
        << "  --fps <value>          发送速率。默认: 1\n"
        << "  --count <n>            发送帧数。0 表示持续发送直到 Ctrl+C。默认: 5\n"
        << "  --timeout-ms <n>       单次请求超时。默认: 3000\n"
        << "  --width <n>            可选的帧宽度元数据\n"
        << "  --height <n>           可选的帧高度元数据\n";
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

// 解析浮点型参数
bool ParseDouble(const std::string& text, double& out) {
    try {
        std::size_t pos = 0;
        const double value = std::stod(text, &pos);
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
        if (arg == "--server") {
            if (!ReadNextValue(i, argc, argv, options.server)) {
                return false;
            }
        } else if (arg == "--image") {
            if (!ReadNextValue(i, argc, argv, options.image_path)) {
                return false;
            }
        } else if (arg == "--format") {
            if (!ReadNextValue(i, argc, argv, options.image_format)) {
                return false;
            }
        } else if (arg == "--video-id") {
            if (!ReadNextValue(i, argc, argv, options.video_id)) {
                return false;
            }
        } else if (arg == "--prompt") {
            if (!ReadNextValue(i, argc, argv, options.prompt)) {
                return false;
            }
        } else if (arg == "--fps") {
            if (!ReadNextValue(i, argc, argv, value) || !ParseDouble(value, options.fps) ||
                options.fps <= 0.0) {
                return false;
            }
        } else if (arg == "--count") {
            if (!ReadNextValue(i, argc, argv, value) || !ParseInt(value, options.count) ||
                options.count < 0) {
                return false;
            }
        } else if (arg == "--timeout-ms") {
            if (!ReadNextValue(i, argc, argv, value) || !ParseInt(value, options.timeout_ms) ||
                options.timeout_ms <= 0) {
                return false;
            }
        } else if (arg == "--width") {
            if (!ReadNextValue(i, argc, argv, value) || !ParseInt(value, options.width) ||
                options.width < 0) {
                return false;
            }
        } else if (arg == "--height") {
            if (!ReadNextValue(i, argc, argv, value) || !ParseInt(value, options.height) ||
                options.height < 0) {
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

// 根据文件路径推断图片格式
std::string InferImageFormat(const std::string& image_path) {
    if (image_path.empty()) {
        return "png";
    }

    std::string ext = std::filesystem::path(image_path).extension().string();
    if (!ext.empty() && ext[0] == '.') {
        ext.erase(ext.begin());
    }
    for (char& ch : ext) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    if (ext == "jpg") {
        return "jpeg";
    }
    return ext.empty() ? "bin" : ext;
}

// 返回内建的 1x1 PNG 图片数据
std::vector<std::uint8_t> BuiltInTinyPng() {
    static constexpr std::uint8_t kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
        0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
        0x42, 0x60, 0x82
    };
    return std::vector<std::uint8_t>(std::begin(kPng), std::end(kPng));
}

// 以二进制模式读取文件所有字节
bool ReadFileBytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return false;
    }
    input.seekg(0, std::ios::beg);

    out.resize(static_cast<std::size_t>(size));
    if (!out.empty()) {
        input.read(reinterpret_cast<char*>(out.data()), size);
    }
    return input.good() || input.eof();
}

// 返回当前 Unix 时间戳（毫秒）
std::int64_t UnixTimeMs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
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

    // 推断图片格式
    if (options.image_format.empty()) {
        options.image_format = InferImageFormat(options.image_path);
    }

    // 加载图片数据
    std::vector<std::uint8_t> image_data;
    if (options.image_path.empty()) {
        image_data = BuiltInTinyPng();
        options.width = options.width > 0 ? options.width : 1;
        options.height = options.height > 0 ? options.height : 1;
    } else if (!ReadFileBytes(options.image_path, image_data)) {
        std::cerr << "read image file failed: " << options.image_path << "\n";
        return 2;
    }

    if (image_data.empty()) {
        std::cerr << "image data is empty\n";
        return 3;
    }

    // 注册信号处理
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    // 创建客户端并计算发送间隔
    FrameIngestClient sender(options.server);
    const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / options.fps));

    std::cout << "send frames to " << options.server
              << ", format=" << options.image_format
              << ", bytes=" << image_data.size()
              << ", fps=" << options.fps
              << ", count=" << options.count << "\n";

    // 循环发送帧
    auto next_tick = std::chrono::steady_clock::now();
    int sent = 0;
    while (!g_stop_requested.load() && (options.count == 0 || sent < options.count)) {
        next_tick += interval;

        // 构建帧发送选项
        FrameSendOptions frame;
        frame.video_id = options.video_id;
        frame.frame_index = static_cast<std::uint64_t>(sent);
        frame.timestamp_ms = static_cast<std::int64_t>(
            (1000.0 * static_cast<double>(sent)) / options.fps);
        frame.image_format = options.image_format;
        frame.image_data = image_data;
        frame.width = options.width;
        frame.height = options.height;
        frame.prompt = options.prompt;
        frame.metadata["source"] = "cpp_timer_demo";
        frame.metadata["sent_at_unix_ms"] = std::to_string(UnixTimeMs());

        // 发送帧并处理响应
        FrameAck ack;
        sender.SendFrame(frame, &ack, options.timeout_ms);
        std::cout << "ack frame=" << ack.frame_id()
                  << " ok=" << (ack.ok() ? "true" : "false")
                  << " bytes=" << ack.received_bytes()
                  << " message=" << ack.message() << "\n";

        ++sent;
        std::this_thread::sleep_until(next_tick);
    }

    std::cout << "sender stopped after " << sent << " frames\n";
    return 0;
}
