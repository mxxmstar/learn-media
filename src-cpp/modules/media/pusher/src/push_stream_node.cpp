#include "pusher/push_stream_node.h"

#include <utility>
#include <vector>

#include "common/log/logmanager.h"

namespace pipeline {

PushStreamNode::PushStreamNode(PipelineOptions options, std::shared_ptr<PipelineState> state)
    : options_(std::move(options)), state_(std::move(state)) {}

bool PushStreamNode::Init() {
    return true;
}

bool PushStreamNode::Start() {
    return true;
}

void PushStreamNode::Stop() {
    if (pusher_) {
        pusher_->Close();
    }
    connected_ = false;
}

void PushStreamNode::Deinit() {
    Stop();
}

std::string PushStreamNode::Name() const {
    return "push_stream";
}

void PushStreamNode::Process(PacketMessage packet) {
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

bool PushStreamNode::ensureConnected() {
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
        LOG_MAIN_WARN_AT("encoder config is not ready for pusher");
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
        LOG_MAIN_ERROR_AT("connect output failed: {}", options_.output_url);
        return false;
    }

    connected_ = true;
    return true;
}

} // namespace pipeline
