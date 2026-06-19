#include "encoder/encode_node.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "common/log/logmanager.h"

namespace pipeline {
namespace {

int SelectFpsNum(const PipelineOptions& options, const StreamInfo& info) {
    if (info.fps >= 1.0f) {
        return static_cast<int>(info.fps + 0.5f);
    }
    return options.fallback_fps_num > 0 ? options.fallback_fps_num : 25;
}

int SelectFpsDen(const PipelineOptions& options) {
    return options.fallback_fps_den > 0 ? options.fallback_fps_den : 1;
}

} // namespace

EncodeNode::EncodeNode(PipelineOptions options,
                       std::shared_ptr<PipelineState> state)
    : options_(std::move(options)), state_(std::move(state)) {}

bool EncodeNode::Init() {
    return true;
}

bool EncodeNode::Start() {
    return true;
}

void EncodeNode::Stop() {}

void EncodeNode::Deinit() {
    encoder_.Close();
    encoder_opened_ = false;
}

std::string EncodeNode::Name() const {
    return "encode";
}

void EncodeNode::Process(FrameMessage frame) {
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

bool EncodeNode::EnsureOpen(const FrameMessage& frame) {
    if (encoder_opened_) {
        return true;
    }

    StreamInfo stream_info;
    if (!state_->TryGetStreamInfo(stream_info)) {
        LOG_MAIN_ERROR_AT("stream info is not ready for encoder");
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
        LOG_MAIN_ERROR_AT("invalid encoder output size {}x{}", config.width, config.height);
        return false;
    }

    if (!encoder_.Open(config)) {
        LOG_MAIN_ERROR_AT("encoder open failed");
        return false;
    }

    std::vector<std::uint8_t> extra_data;
    if (stream_info.codec_type == config.codec_type) {
        extra_data = stream_info.extra_data;
    }
    state_->SetEncoderConfig(config, std::move(extra_data));
    encoder_opened_ = true;
    return true;
}

} // namespace pipeline
