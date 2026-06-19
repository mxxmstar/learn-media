#pragma once

#include <memory>
#include <string>

#include "encoder/ffmpeg_encoder.hpp"
#include "node/i_node.h"
#include "node/transform_node.h"
#include "pipeline/pipeline_types.h"

namespace pipeline {

class EncodeNode : public runtime::INode,
                   public runtime::TransformNode<FrameMessage, PacketMessage> {
public:
    EncodeNode(PipelineOptions options, std::shared_ptr<PipelineState> state);

    bool Init() override;
    bool Start() override;
    void Stop() override;
    void Deinit() override;
    std::string Name() const override;

protected:
    void Process(FrameMessage frame) override;

private:
    bool EnsureOpen(const FrameMessage& frame);

    PipelineOptions options_;
    std::shared_ptr<PipelineState> state_;
    FFmpegEncoder encoder_;
    bool encoder_opened_{false};
};

} // namespace pipeline
