#pragma once

#include <memory>
#include <string>

#include "decoder/ffmpeg_decoder.hpp"
#include "node/i_node.h"
#include "node/transform_node.h"
#include "pipeline/pipeline_types.h"

namespace pipeline {

// A single encoded packet may produce zero, one, or many decoded frames.
class DecodeNode : public runtime::INode,
                   public runtime::TransformNode<PacketMessage, FrameMessage> {
public:
    explicit DecodeNode(std::shared_ptr<PipelineState> state);

    bool Init() override;
    bool Start() override;
    void Stop() override;
    void Deinit() override;
    std::string Name() const override;

protected:
    void Process(PacketMessage packet) override;

private:
    std::shared_ptr<PipelineState> state_;
    FFmpegDecoder decoder_;
    bool decoder_opened_{false};
};

} // namespace pipeline
