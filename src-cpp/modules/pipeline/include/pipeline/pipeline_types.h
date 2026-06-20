#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "defines/media_frame.hpp"
#include "defines/media_packet.hpp"
#include "encoder/i_encoder.hpp"
#include "inferenceinfo/result.h"
#include "stream/stream_info.h"

namespace pipeline {

using PacketMessage = std::shared_ptr<MediaPacket>;
using FrameMessage = std::shared_ptr<MediaFrame>;

struct InferenceMessage {
    FrameMessage frame;
    FrameResult result{};
};
using InferenceMessagePtr = std::shared_ptr<InferenceMessage>;

struct PipelineOptions {
    std::string input_url{};
    std::string output_url{};
    std::string model_path;

    std::string output_format;
    std::string pull_rtsp_transport{"tcp"};
    std::string push_rtsp_transport{"tcp"};
    std::string encoder_name;
    std::string preset{"ultrafast"};
    std::string tune{"zerolatency"};
    std::string model_input_layout;
    std::string model_name{"yolo"};

    CodecType output_codec{CodecType::H264};
    PixelFormat model_pixel_format{PixelFormat::kRGB24};
    std::int64_t bitrate{2'000'000};
    int fallback_fps_num{25};
    int fallback_fps_den{1};
    int gop_size{50};
    int max_b_frames{0};
    int connect_timeout_ms{5000};
    int read_timeout_ms{10000};
    int class_count{80};
    std::uint32_t infer_request_count{2};
    float preprocess_scale{255.0f};
    bool low_latency{true};
};

struct PipelineStats {
    std::atomic<std::uint64_t> pulled_packets{0};
    std::atomic<std::uint64_t> decoded_frames{0};
    std::atomic<std::uint64_t> inference_submitted{0};
    std::atomic<std::uint64_t> inferred_frames{0};
    std::atomic<std::uint64_t> detected_objects{0};
    std::atomic<std::uint64_t> osd_frames{0};
    std::atomic<std::uint64_t> encoded_packets{0};
    std::atomic<std::uint64_t> pushed_packets{0};
    std::atomic<std::uint64_t> decode_errors{0};
    std::atomic<std::uint64_t> inference_errors{0};
    std::atomic<std::uint64_t> osd_errors{0};
    std::atomic<std::uint64_t> encode_errors{0};
    std::atomic<std::uint64_t> push_errors{0};
    std::atomic_bool source_finished{false};
};

class PipelineState {
public:
    void SetStreamInfo(StreamInfo info) {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_info_ = std::move(info);
        stream_info_ready_ = true;
    }

    bool TryGetStreamInfo(StreamInfo& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stream_info_ready_) {
            return false;
        }
        out = stream_info_;
        return true;
    }

    void SetEncoderConfig(EncoderConfig config, std::vector<std::uint8_t> extra_data) {
        std::lock_guard<std::mutex> lock(mutex_);
        encoder_config_ = std::move(config);
        pusher_extra_data_ = std::move(extra_data);
        encoder_config_ready_ = true;
    }

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

    PipelineStats stats;

private:
    mutable std::mutex mutex_;
    StreamInfo stream_info_;
    EncoderConfig encoder_config_;
    std::vector<std::uint8_t> pusher_extra_data_;
    bool stream_info_ready_{false};
    bool encoder_config_ready_{false};
};

} // namespace pipeline
