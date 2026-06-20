#include "puller/pull_stream_node.h"

#include <utility>

#include "common/log/logmanager.h"

namespace pipeline {

PullStreamNode::PullStreamNode(PipelineOptions options, std::shared_ptr<PipelineState> state)
    : options_(std::move(options)), state_(std::move(state)) {

}

bool PullStreamNode::Init() {
    puller_.SetConnectTimeoutMs(options_.connect_timeout_ms);
    puller_.SetReadTimeoutMs(options_.read_timeout_ms);
    puller_.SetLowLatency(options_.low_latency);
    puller_.SetRtspTransport(options_.pull_rtsp_transport);
    puller_.SetEventCallback([](const std::string& event) {
        LOG_MAIN_WARN_AT("puller event: {}", event);
    });
    return true;
}

bool PullStreamNode::Start() {
    if (running_.exchange(true)) {
        return true;
    }

    if (!puller_.Open(options_.input_url)) {
        running_.store(false);
        LOG_MAIN_ERROR_AT("open input failed: {}", options_.input_url);
        return false;
    }

    StreamInfo info = puller_.GetStreamInfo();
    if (info.media_type != MediaType::VIDEO || info.codec_type == CodecType::UNKNOWN ||
        info.width <= 0 || info.height <= 0) {
        running_.store(false);
        puller_.Close();
        LOG_MAIN_ERROR_AT("invalid input stream info, media_type: {}, codec_type: {}, width: {}, height: {}",
                          static_cast<int>(info.media_type), static_cast<int>(info.codec_type), info.width, info.height);
        return false;
    }

    state_->SetStreamInfo(info);
    read_thread_ = std::thread([this]() { ReadLoop(); });
    return true;
}

void PullStreamNode::Stop() {
    running_.store(false);
    puller_.Close();
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
}

void PullStreamNode::Deinit() {
    Stop();
}

std::string PullStreamNode::Name() const {
    return "pull_stream";
}

void PullStreamNode::ReadLoop() {
    while (running_.load()) {
        PacketMessage packet;
        const bool ok = puller_.ReadPacket(packet);
        if (!running_.load()) {
            break;
        }
        if (!ok) {
            LOG_MAIN_WARN_AT("pull stream stopped");
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

} // namespace pipeline
