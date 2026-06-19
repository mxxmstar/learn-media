#include "decoder/decode_node.h"

#include <utility>

#include "common/log/logmanager.h"

namespace pipeline {

DecodeNode::DecodeNode(std::shared_ptr<PipelineState> state)
    : state_(std::move(state)) {}

bool DecodeNode::Init() {
    decoder_.SetFrameCallback([this](FrameMessage frame) {
        if (!frame) {
            return;
        }
        state_->stats.decoded_frames.fetch_add(1);
        Emit(std::move(frame));
    });
    return true;
}

bool DecodeNode::Start() {
    return true;
}

void DecodeNode::Stop() {}

void DecodeNode::Deinit() {
    decoder_.SetFrameCallback(nullptr);
    decoder_.Close();
    decoder_opened_ = false;
}

std::string DecodeNode::Name() const {
    return "decode";
}

void DecodeNode::Process(PacketMessage packet) {
    if (!packet) {
        return;
    }

    if (!decoder_opened_) {
        StreamInfo info;
        if (!state_->TryGetStreamInfo(info)) {
            LOG_MAIN_WARN_AT("stream info is not ready, dropping packet");
            return;
        }
        if (!decoder_.Open(info)) {
            state_->stats.decode_errors.fetch_add(1);
            LOG_MAIN_ERROR_AT("decoder open failed");
            return;
        }
        decoder_opened_ = true;
    }

    if (!decoder_.Decode(std::move(packet))) {
        state_->stats.decode_errors.fetch_add(1);
    }
}

} // namespace pipeline
