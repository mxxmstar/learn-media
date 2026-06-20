#include "common/log/logmanager.h"
#include "decoder/ffmpeg_decoder.hpp"
#include "encoder/ffmpeg_encoder.hpp"
#include "node/i_node.h"
#include "node/sink_node.h"
#include "node/source_node.h"
#include "node/transform_node.h"
#include "puller/ffmpeg_puller.hpp"
#include "pusher/i_pusher.hpp"
#include "runtime/runtime.h"
#include "stream/stream_info.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

// 类型别名：将数据包/帧以 shared_ptr 形式在节点之间传递
using PacketMessage = std::shared_ptr<MediaPacket>;
using FrameMessage = std::shared_ptr<MediaFrame>;

// 全局停止标志，被信号处理器设置为 1
volatile std::sig_atomic_t g_stop_requested = 0;

// 信号处理函数：收到 SIGINT/SIGTERM 时通知主循环退出
void HandleSignal(int) {
    g_stop_requested = 1;
}

// -----------------------------------------------------------------------
// DemoOptions：存放所有命令行选项的默认值
// -----------------------------------------------------------------------
struct DemoOptions {
    // RTSP 拉流地址（输入源）
    std::string input_url{"rtsp://192.168.66.218/live/mainstream"};
    // RTSP 推流地址（输出目标）
    std::string output_url{"rtsp://127.0.0.1:554/live/proxy_cam1"};
    // 输出封装格式（如 rtsp / flv，空则自动推断）
    std::string output_format;
    // 拉流的 RTSP 传输协议：tcp / udp
    std::string pull_rtsp_transport{"tcp"};
    // 推流的 RTSP 传输协议：tcp / udp
    std::string push_rtsp_transport{"tcp"};
    // 编码器名称（如 libx264，空则使用默认）
    std::string encoder_name;
    // x264 预设：ultrafast / fast / medium / slow
    std::string preset{"ultrafast"};
    // x264 调优：zerolatency / film / animation
    std::string tune{"zerolatency"};

    // 输出编码格式（H264 / H265）
    CodecType output_codec{CodecType::H264};
    // 输出视频码率（bps）
    std::int64_t bitrate{2'000'000};
    // 输出帧率分子（0 表示跟随源流）
    int fps_num{0};
    // 输出帧率分母
    int fps_den{1};
    // GOP 大小（关键帧间隔）
    int gop_size{50};
    // 最大 B 帧数量
    int max_b_frames{0};
    // 运行时长（秒），0 表示持续到 Ctrl+C
    int duration_seconds{60};
    // 拉流连接超时（毫秒）
    int connect_timeout_ms{5000};
    // 拉流读取超时（毫秒）
    int read_timeout_ms{10000};
    // 是否启用低延迟模式
    bool low_latency{true};
    // 是否只显示帮助
    bool show_help{false};
};

// -----------------------------------------------------------------------
// PipelineStats：流水线各阶段的原子统计计数器
// -----------------------------------------------------------------------
struct PipelineStats {
    // 已拉取的数据包数
    std::atomic<std::uint64_t> pulled_packets{0};
    // 已解码的帧数
    std::atomic<std::uint64_t> decoded_frames{0};
    // 已编码的数据包数
    std::atomic<std::uint64_t> encoded_packets{0};
    // 已推送的数据包数
    std::atomic<std::uint64_t> pushed_packets{0};
    // 解码错误次数
    std::atomic<std::uint64_t> decode_errors{0};
    // 编码错误次数
    std::atomic<std::uint64_t> encode_errors{0};
    // 推送错误次数
    std::atomic<std::uint64_t> push_errors{0};
    // 源流是否已结束
    std::atomic_bool source_finished{false};
};

// -----------------------------------------------------------------------
// PipelineState：跨节点共享的流水线状态
// 线程安全：通过 mutex 保护 StreamInfo 和 EncoderConfig 的读写
// -----------------------------------------------------------------------
class PipelineState {
public:
    // 设置源流信息（由 PullStreamNode 在启动后调用）
    void SetStreamInfo(StreamInfo info) {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_info_ = std::move(info);
        stream_info_ready_ = true;
    }

    // 尝试获取源流信息（DecodeNode / EncodeNode 在首次处理时调用）
    bool TryGetStreamInfo(StreamInfo& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stream_info_ready_) {
            return false;
        }
        out = stream_info_;
        return true;
    }

    // 设置编码器配置（由 EncodeNode 在首次编码成功后调用）
    void SetEncoderConfig(EncoderConfig config, std::vector<std::uint8_t> extra_data) {
        std::lock_guard<std::mutex> lock(mutex_);
        encoder_config_ = std::move(config);
        pusher_extra_data_ = std::move(extra_data);
        encoder_config_ready_ = true;
    }

    // 尝试获取编码器配置（PushStreamNode 在首次推送时调用）
    bool TryGetEncoderConfig(EncoderConfig& config,
                             std::vector<std::uint8_t>& extra_data) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!encoder_config_ready_) {
            return false;
        }
        config = encoder_config_;
        extra_data = pusher_extra_data_;
        return true;
    }

    // 流水线统计信息（无需锁保护，因使用原子操作）
    PipelineStats stats;

private:
    mutable std::mutex mutex_;
    StreamInfo stream_info_;                          // 源流元数据
    EncoderConfig encoder_config_;                    // 编码器配置
    std::vector<std::uint8_t> pusher_extra_data_;     // 编码器额外数据（如 H.264 SPS/PPS）
    bool stream_info_ready_{false};                   // stream_info_ 是否已就绪
    bool encoder_config_ready_{false};                // encoder_config_ 是否已就绪
};

// -----------------------------------------------------------------------
// 工具函数
// -----------------------------------------------------------------------

// 将字符串转为小写（用于不区分大小写的比较）
std::string ToLower(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

// 解析 int（要求将整个字符串完全解析，不允许尾部多余字符）
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

// 解析 int64_t（处理码率等大整数）
bool ParseInt64(const std::string& text, std::int64_t& out) {
    try {
        std::size_t pos = 0;
        const std::int64_t value = std::stoll(text, &pos, 10);
        if (pos != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

// 解析编解码器名称（h264/avc -> H264；h265/hevc -> H265）
bool ParseCodec(const std::string& text, CodecType& out) {
    const std::string codec = ToLower(text);
    if (codec == "h264" || codec == "avc") {
        out = CodecType::H264;
        return true;
    }
    if (codec == "h265" || codec == "hevc") {
        out = CodecType::H265;
        return true;
    }
    return false;
}

// 解析帧率字符串：支持 "25" 或 "30000/1001" 两种格式
bool ParseFps(const std::string& text, int& num, int& den) {
    const std::size_t slash = text.find('/');
    if (slash == std::string::npos) {
        // 纯整数形式
        int parsed = 0;
        if (!ParseInt(text, parsed) || parsed <= 0) {
            return false;
        }
        num = parsed;
        den = 1;
        return true;
    }

    // 分数形式：num/den
    int parsed_num = 0;
    int parsed_den = 0;
    if (!ParseInt(text.substr(0, slash), parsed_num) ||
        !ParseInt(text.substr(slash + 1), parsed_den) ||
        parsed_num <= 0 || parsed_den <= 0) {
        return false;
    }

    num = parsed_num;
    den = parsed_den;
    return true;
}

// 读取选项的下一个参数值，失败时返回 false
bool ReadOptionValue(int& index, int argc, char** argv, std::string& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return true;
}

// -----------------------------------------------------------------------
// PrintUsage：打印命令行帮助信息
// -----------------------------------------------------------------------
void PrintUsage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe << " [--input <pull_url>] [--output <push_url>] [options]\n\n"
        << "Options:\n"
        << "  --seconds <n>              Stop after n seconds. 0 means run until Ctrl+C.\n"
        << "  --codec <h264|h265>        Output codec. Default: h264.\n"
        << "  --bitrate <bps>            Output video bitrate. Default: 2000000.\n"
        << "  --fps <n|num/den>          Output frame rate. Default: 25.\n"
        << "  --gop <n>                  GOP size. Default: 50.\n"
        << "  --encoder <name>           FFmpeg encoder name, such as libx264.\n"
        << "  --format <name>            Output muxer format, such as rtsp or flv.\n"
        << "  --pull-rtsp-transport <v>  Pull RTSP transport. Default: tcp.\n"
        << "  --push-rtsp-transport <v>  Push RTSP transport. Default: tcp.\n"
        << "  --connect-timeout-ms <n>   Pull connect timeout. Default: 5000.\n"
        << "  --read-timeout-ms <n>      Pull read timeout. Default: 10000.\n"
        << "  --no-low-latency           Disable low-latency pull options.\n\n"
        << "Defaults: input=rtsp://127.0.0.1/live/in, "
           "output=rtsp://127.0.0.1/live/out, "
           "seconds=60, codec=h264, bitrate=2000000\n\n"
        << "Examples:\n"
        << "  " << exe << "\n"
        << "  " << exe
        << " --input rtsp://10.0.0.1/live/in"
        << " --output rtsp://10.0.0.1/live/out"
        << " --seconds 120\n";
}

// -----------------------------------------------------------------------
// ParseArgs：解析命令行参数，填充 DemoOptions
// 返回 false 表示解析失败（此时已打印错误用法）
// -----------------------------------------------------------------------
bool ParseArgs(int argc, char** argv, DemoOptions& options) {
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;

        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            return true;
        } else if (arg == "--input") {
            if (!ReadOptionValue(i, argc, argv, options.input_url)) {
                return false;
            }
        } else if (arg == "--output") {
            if (!ReadOptionValue(i, argc, argv, options.output_url)) {
                return false;
            }
        } else if (arg == "--seconds") {
            if (!ReadOptionValue(i, argc, argv, value) ||
                !ParseInt(value, options.duration_seconds) ||
                options.duration_seconds < 0) {
                return false;
            }
        } else if (arg == "--codec") {
            if (!ReadOptionValue(i, argc, argv, value) ||
                !ParseCodec(value, options.output_codec)) {
                return false;
            }
        } else if (arg == "--bitrate") {
            if (!ReadOptionValue(i, argc, argv, value) ||
                !ParseInt64(value, options.bitrate) ||
                options.bitrate <= 0) {
                return false;
            }
        } else if (arg == "--fps") {
            if (!ReadOptionValue(i, argc, argv, value) ||
                !ParseFps(value, options.fps_num, options.fps_den)) {
                return false;
            }
        } else if (arg == "--gop") {
            if (!ReadOptionValue(i, argc, argv, value) ||
                !ParseInt(value, options.gop_size) ||
                options.gop_size <= 0) {
                return false;
            }
        } else if (arg == "--encoder") {
            if (!ReadOptionValue(i, argc, argv, options.encoder_name)) {
                return false;
            }
        } else if (arg == "--format") {
            if (!ReadOptionValue(i, argc, argv, options.output_format)) {
                return false;
            }
        } else if (arg == "--pull-rtsp-transport") {
            if (!ReadOptionValue(i, argc, argv, options.pull_rtsp_transport)) {
                return false;
            }
        } else if (arg == "--push-rtsp-transport") {
            if (!ReadOptionValue(i, argc, argv, options.push_rtsp_transport)) {
                return false;
            }
        } else if (arg == "--connect-timeout-ms") {
            if (!ReadOptionValue(i, argc, argv, value) ||
                !ParseInt(value, options.connect_timeout_ms) ||
                options.connect_timeout_ms < 0) {
                return false;
            }
        } else if (arg == "--read-timeout-ms") {
            if (!ReadOptionValue(i, argc, argv, value) ||
                !ParseInt(value, options.read_timeout_ms) ||
                options.read_timeout_ms < 0) {
                return false;
            }
        } else if (arg == "--no-low-latency") {
            options.low_latency = false;
        } else if (!arg.empty() && arg[0] == '-') {
            // 遇到未知选项，返回失败
            return false;
        } else {
            // 非选项参数（以 --xxx 为界）暂存
            positional.push_back(arg);
        }
    }

    // 若 --input/--output 未通过命名选项设置，尝试从位置参数回退
    if (options.input_url.empty() && !positional.empty()) {
        options.input_url = positional[0];
    }
    if (options.output_url.empty() && positional.size() > 1) {
        options.output_url = positional[1];
    }

    return true;
}

// -----------------------------------------------------------------------
// SelectFpsNum / SelectFpsDen：选择输出帧率
// 优先使用用户指定的 fps_num；否则从源流信息中获取
// -----------------------------------------------------------------------
int SelectFpsNum(const DemoOptions& options, const StreamInfo& info) {
    if (options.fps_num > 0) {
        return options.fps_num;
    }
    if (info.fps >= 1.0f) {
        return static_cast<int>(info.fps + 0.5f);
    }
    return 25;
}

int SelectFpsDen(const DemoOptions& options) {
    return options.fps_den > 0 ? options.fps_den : 1;
}

// -----------------------------------------------------------------------
// PullStreamNode：拉流节点（数据源）
// 继承 SourceNode<PacketMessage>，作为流水线的起点
// -----------------------------------------------------------------------
class PullStreamNode : public runtime::INode,
                       public runtime::SourceNode<PacketMessage> {
public:
    PullStreamNode(DemoOptions options, std::shared_ptr<PipelineState> state)
        : options_(std::move(options)), state_(std::move(state)) {}

    // Init：配置拉流参数（超时、传输协议等）
    bool Init() override {
        puller_.SetConnectTimeoutMs(options_.connect_timeout_ms);
        puller_.SetReadTimeoutMs(options_.read_timeout_ms);
        puller_.SetLowLatency(options_.low_latency);
        puller_.SetRtspTransport(options_.pull_rtsp_transport);
        puller_.SetEventCallback([](const std::string& event) {
            LOG_MAIN_WARN_AT("puller event: {}", event);
        });
        return true;
    }

    // Start：打开拉流连接，校验流信息，启动读取线程
    bool Start() override {
        if (running_.exchange(true)) {
            return true;
        }

        if (!puller_.Open(options_.input_url)) {
            running_.store(false);
            LOG_MAIN_ERROR_AT("PullStreamNode: open input failed: {}", options_.input_url);
            return false;
        }

        StreamInfo info = puller_.GetStreamInfo();
        if (info.media_type != MediaType::VIDEO || info.codec_type == CodecType::UNKNOWN ||
            info.width <= 0 || info.height <= 0) {
            running_.store(false);
            puller_.Close();
            LOG_MAIN_ERROR_AT("PullStreamNode: invalid stream info");
            return false;
        }

        // 向下游节点共享源流信息
        state_->SetStreamInfo(info);
        read_thread_ = std::thread([this]() { ReadLoop(); });
        return true;
    }

    // Stop：停止拉流，等待读取线程退出
    void Stop() override {
        running_.store(false);
        puller_.Close();
        if (read_thread_.joinable()) {
            read_thread_.join();
        }
    }

    void Deinit() override {
        Stop();
    }

    std::string Name() const override {
        return "pull_stream";
    }

private:
    // ReadLoop：不断从拉流器中读取数据包并发送到下一节点
    void ReadLoop() {
        while (running_.load()) {
            PacketMessage packet;
            const bool ok = puller_.ReadPacket(packet);

            if (!running_.load()) {
                break;
            }
            if (!ok) {
                LOG_MAIN_WARN_AT("PullStreamNode: ReadPacket stopped");
                break;
            }
            if (!packet) {
                continue;
            }

            state_->stats.pulled_packets.fetch_add(1);
            Emit(std::move(packet));
        }

        state_->stats.source_finished.store(true);
        running_.store(false);
    }

    DemoOptions options_;
    std::shared_ptr<PipelineState> state_;
    FFmpegPuller puller_;       // FFmpeg 拉流器实例
    std::atomic_bool running_{false};
    std::thread read_thread_;
};

// -----------------------------------------------------------------------
// DecodeNode：解码节点
// 继承 TransformNode<PacketMessage, FrameMessage>，将压缩包解码为原始帧
// -----------------------------------------------------------------------
class DecodeNode : public runtime::INode,
                   public runtime::TransformNode<PacketMessage, FrameMessage> {
public:
    explicit DecodeNode(std::shared_ptr<PipelineState> state)
        : state_(std::move(state)) {}

    // Init：设置解码完成后的回调，将帧发送到下一节点
    bool Init() override {
        decoder_.SetFrameCallback([this](FrameMessage frame) {
            if (!frame) {
                return;
            }
            state_->stats.decoded_frames.fetch_add(1);
            Emit(std::move(frame));
        });
        return true;
    }

    bool Start() override {
        return true;
    }

    void Stop() override {}

    void Deinit() override {
        decoder_.SetFrameCallback(nullptr);
        decoder_.Close();
        decoder_opened_ = false;
    }

    std::string Name() const override {
        return "decode";
    }

protected:
    // Process：收到数据包后，按需打开解码器并送入解码
    void Process(PacketMessage packet) override {
        if (!packet) {
            return;
        }

        // 延迟打开：等收到第一个包时才从 PipelineState 获取流信息并初始化解码器
        if (!decoder_opened_) {
            StreamInfo info;
            if (!state_->TryGetStreamInfo(info)) {
                LOG_MAIN_WARN_AT("DecodeNode: stream info is not ready, dropping packet");
                return;
            }
            if (!decoder_.Open(info)) {
                state_->stats.decode_errors.fetch_add(1);
                LOG_MAIN_ERROR_AT("DecodeNode: decoder open failed");
                return;
            }
            decoder_opened_ = true;
        }

        if (!decoder_.Decode(std::move(packet))) {
            state_->stats.decode_errors.fetch_add(1);
        }
    }

private:
    std::shared_ptr<PipelineState> state_;
    FFmpegDecoder decoder_;    // FFmpeg 解码器实例
    bool decoder_opened_{false};
};

// -----------------------------------------------------------------------
// EncodeNode：编码节点
// 继承 TransformNode<FrameMessage, PacketMessage>，将原始帧重新编码
// -----------------------------------------------------------------------
class EncodeNode : public runtime::INode,
                   public runtime::TransformNode<FrameMessage, PacketMessage> {
public:
    EncodeNode(DemoOptions options, std::shared_ptr<PipelineState> state)
        : options_(std::move(options)), state_(std::move(state)) {}

    bool Init() override {
        return true;
    }

    bool Start() override {
        return true;
    }

    void Stop() override {}

    void Deinit() override {
        encoder_.Close();
        encoder_opened_ = false;
    }

    std::string Name() const override {
        return "encode";
    }

protected:
    // Process：收到原始帧后，确保编码器已打开，然后进行编码
    void Process(FrameMessage frame) override {
        if (!frame) {
            return;
        }

        if (!EnsureOpen(frame)) {
            state_->stats.encode_errors.fetch_add(1);
            return;
        }

        std::vector<PacketPtr> packets;
        if (!encoder_.Encode(std::move(frame), packets)) {
            state_->stats.encode_errors.fetch_add(1);
            return;
        }

        for (auto& packet : packets) {
            if (!packet) {
                continue;
            }
            state_->stats.encoded_packets.fetch_add(1);
            Emit(std::move(packet));
        }
    }

private:
    // EnsureOpen：按需打开编码器
    // 使用收到的第一帧的宽高、像素格式等信息构造 EncoderConfig
    bool EnsureOpen(const FrameMessage& frame) {
        if (encoder_opened_) {
            return true;
        }

        // 等待源流信息就绪
        StreamInfo stream_info;
        if (!state_->TryGetStreamInfo(stream_info)) {
            LOG_MAIN_ERROR_AT("EncodeNode: stream info is not ready");
            return false;
        }

        EncoderConfig config;
        config.media_type = MediaType::VIDEO;
        config.codec_type = options_.output_codec;
        config.pixel_format = frame->pixel_format == PixelFormat::kUnknown
            ? PixelFormat::kI420
            : frame->pixel_format;
        config.width = frame->width > 0 ? frame->width : stream_info.width;
        config.height = frame->height > 0 ? frame->height : stream_info.height;
        config.fps_num = SelectFpsNum(options_, stream_info);
        config.fps_den = SelectFpsDen(options_);
        config.bitrate = options_.bitrate;
        config.gop_size = options_.gop_size;
        config.max_b_frames = options_.max_b_frames;
        config.time_base_num = config.fps_den;
        config.time_base_den = config.fps_num;
        config.encoder_name = options_.encoder_name;
        config.preset = options_.preset;
        config.tune = options_.tune;
        config.global_header = false;

        if (config.width <= 0 || config.height <= 0) {
            LOG_MAIN_ERROR_AT("EncodeNode: invalid output size {}x{}",
                              config.width, config.height);
            return false;
        }

        if (!encoder_.Open(config)) {
            LOG_MAIN_ERROR_AT("EncodeNode: encoder open failed");
            return false;
        }

        // 若输出编码与源流编码相同，透传源流的 extra_data（如 H.264 SPS/PPS）
        std::vector<std::uint8_t> extra_data;
        if (stream_info.codec_type == config.codec_type) {
            extra_data = stream_info.extra_data;
        }
        state_->SetEncoderConfig(config, std::move(extra_data));
        encoder_opened_ = true;
        return true;
    }

    DemoOptions options_;
    std::shared_ptr<PipelineState> state_;
    FFmpegEncoder encoder_;    // FFmpeg 编码器实例
    bool encoder_opened_{false};
};

// -----------------------------------------------------------------------
// PushStreamNode：推流节点（数据汇）
// 继承 SinkNode<PacketMessage>，将编码后的数据包推送到 RTSP 服务器
// -----------------------------------------------------------------------
class PushStreamNode : public runtime::INode,
                       public runtime::SinkNode<PacketMessage> {
public:
    PushStreamNode(DemoOptions options, std::shared_ptr<PipelineState> state)
        : options_(std::move(options)), state_(std::move(state)) {}

    bool Init() override {
        return true;
    }

    bool Start() override {
        return true;
    }

    // Stop：关闭推流器连接
    void Stop() override {
        if (pusher_) {
            pusher_->Close();
        }
        connected_ = false;
    }

    void Deinit() override {
        Stop();
    }

    std::string Name() const override {
        return "push_stream";
    }

protected:
    // Process：收到编码后的数据包，确保推流器已连接后发送
    void Process(PacketMessage packet) override {
        if (!packet) {
            return;
        }

        if (!ensureConnected()) {
            state_->stats.push_errors.fetch_add(1);
            return;
        }

        if (!pusher_->Send(*packet)) {
            state_->stats.push_errors.fetch_add(1);
            return;
        }

        state_->stats.pushed_packets.fetch_add(1);
    }

private:
    // EnsureConnected：按需建立推流连接
    // 等待编码器配置就绪后，用 EncoderConfig 初始化 PusherConfig
    // 连接失败时每 2 秒重试一次
    bool ensureConnected() {
        if (connected_) {
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        if (last_connect_attempt_.time_since_epoch().count() != 0 &&
            now - last_connect_attempt_ < std::chrono::seconds(2)) {
            return false;
        }
        last_connect_attempt_ = now;

        EncoderConfig encoder_config;
        std::vector<std::uint8_t> extra_data;
        if (!state_->TryGetEncoderConfig(encoder_config, extra_data)) {
            LOG_MAIN_WARN_AT("PushStreamNode: encoder config is not ready");
            return false;
        }

        PusherConfig config;
        config.url = options_.output_url;
        config.format_name = options_.output_format;
        config.rtsp_transport = options_.push_rtsp_transport;
        config.media_type = MediaType::VIDEO;
        config.codec_type = encoder_config.codec_type;
        config.width = encoder_config.width;
        config.height = encoder_config.height;
        config.time_base_num = encoder_config.time_base_num > 0
            ? encoder_config.time_base_num
            : encoder_config.fps_den;
        config.time_base_den = encoder_config.time_base_den > 0
            ? encoder_config.time_base_den
            : encoder_config.fps_num;
        config.extra_data = std::move(extra_data);

        pusher_ = IPusher::Create(std::move(config));
        if (!pusher_ || !pusher_->Connect()) {
            pusher_.reset();
            LOG_MAIN_ERROR_AT("PushStreamNode: connect output failed: {}",
                              options_.output_url);
            return false;
        }

        connected_ = true;
        return true;
    }

    DemoOptions options_;
    std::shared_ptr<PipelineState> state_;
    std::unique_ptr<IPusher> pusher_;                     // 推流器实例
    std::chrono::steady_clock::time_point last_connect_attempt_;  // 上次连接尝试时间
    bool connected_{false};
};

// -----------------------------------------------------------------------
// BuildGraph：构建流水线图
// 创建 4 个执行器（拉流 / 解码 / 编码 / 推流），添加节点，连接边
// -----------------------------------------------------------------------
bool BuildGraph(runtime::Runtime& runtime,
                const DemoOptions& options,
                const std::shared_ptr<PipelineState>& state) {
    // 创建专用执行器，每个执行器绑定到独立的线程池
    auto pull_exec = runtime.CreateExecutor("media_pull", "media_pull_io", 1);
    auto decode_exec = runtime.CreateExecutor("media_decode", "media_decode_cpu", 1);
    auto encode_exec = runtime.CreateExecutor("media_encode", "media_encode_cpu", 1);
    auto push_exec = runtime.CreateExecutor("media_push", "media_push_io", 1);

    auto& graph = runtime.GetGraph();
    if (graph.AddNode<PullStreamNode>("pull", pull_exec, options, state).empty() ||
        graph.AddNode<DecodeNode>("decode", decode_exec, state).empty() ||
        graph.AddNode<EncodeNode>("encode", encode_exec, options, state).empty() ||
        graph.AddNode<PushStreamNode>("push", push_exec, options, state).empty()) {
        LOG_MAIN_ERROR_AT("BuildGraph: AddNode failed");
        return false;
    }

    // 连接各节点，指定消息队列容量与反压策略
    const bool connected =
        graph.Connect<PacketMessage>("pull", "decode",
                                     runtime::TransportType::Queue, 128,
                                     runtime::BackpressurePolicy::DropOldest) &&
        graph.Connect<FrameMessage>("decode", "encode",
                                    runtime::TransportType::Queue, 16,
                                    runtime::BackpressurePolicy::DropOldest) &&
        graph.Connect<PacketMessage>("encode", "push",
                                     runtime::TransportType::Queue, 64,
                                     runtime::BackpressurePolicy::DropOldest);
    if (!connected) {
        LOG_MAIN_ERROR_AT("BuildGraph: Connect failed");
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------
// LogStats：打印流水线各阶段统计数据
// -----------------------------------------------------------------------
void LogStats(const PipelineState& state) {
    LOG_MAIN_INFO_AT(
        "stats: pulled={}, decoded={}, encoded={}, pushed={}, "
        "decode_errors={}, encode_errors={}, push_errors={}",
        state.stats.pulled_packets.load(),
        state.stats.decoded_frames.load(),
        state.stats.encoded_packets.load(),
        state.stats.pushed_packets.load(),
        state.stats.decode_errors.load(),
        state.stats.encode_errors.load(),
        state.stats.push_errors.load());
}

} // namespace

// -----------------------------------------------------------------------
// main：程序入口
// 流程：解析参数 -> 注册信号 -> 构建流水线 -> 启动运行 -> 等待退出
// -----------------------------------------------------------------------
int main(int argc, char** argv) {
    LogManager::getInstance().Init();

    DemoOptions options;
    if (!ParseArgs(argc, argv, options)) {
        PrintUsage(argv[0]);
        LogManager::getInstance().FlushAll();
        return 1;
    }
    if (options.show_help) {
        PrintUsage(argv[0]);
        LogManager::getInstance().FlushAll();
        return 0;
    }

    // 注册信号处理，支持 Ctrl+C 优雅退出
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    auto state = std::make_shared<PipelineState>();
    runtime::Runtime runtime;
    if (!BuildGraph(runtime, options, state)) {
        LogManager::getInstance().FlushAll();
        return 2;
    }

    LOG_MAIN_INFO_AT("runtime transcode demo starting");
    LOG_MAIN_INFO_AT("input: {}", options.input_url);
    LOG_MAIN_INFO_AT("output: {}", options.output_url);

    // 启动所有节点，开始流水线处理
    if (!runtime.Start()) {
        LOG_MAIN_ERROR_AT("runtime.Start failed");
        runtime.ShutdownAllPools();
        LogManager::getInstance().FlushAll();
        return 3;
    }

    // 主循环：每秒检查停止条件
    const auto started_at = std::chrono::steady_clock::now();
    int tick = 0;
    while (!g_stop_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++tick;

        if (tick % 5 == 0) {
            LogStats(*state);
        }
        // 源流结束（如 RTSP 断开或文件读取完毕）
        if (state->stats.source_finished.load()) {
            LOG_MAIN_WARN_AT("source finished, stopping runtime");
            break;
        }
        // 达到指定运行时长
        if (options.duration_seconds > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - started_at);
            if (elapsed.count() >= options.duration_seconds) {
                LOG_MAIN_INFO_AT("duration reached: {} seconds", options.duration_seconds);
                break;
            }
        }
    }

    // 停止流水线，释放资源
    runtime.Stop();
    runtime.ShutdownAllPools();
    LogStats(*state);
    LOG_MAIN_INFO_AT("runtime transcode demo stopped");
    LogManager::getInstance().FlushAll();
    return 0;
}
