#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "node/i_node.h"
#include "node/source_node.h"
#include "pipeline/pipeline_types.h"
#include "puller/ffmpeg_puller.hpp"

namespace pipeline {

class PullStreamNode : public runtime::INode,
                       public runtime::SourceNode<PacketMessage> {
public:
    PullStreamNode(PipelineOptions options, std::shared_ptr<PipelineState> state);

    bool Init() override;
    bool Start() override;
    void Stop() override;
    void Deinit() override;
    std::string Name() const override;

private:
    void ReadLoop();

    PipelineOptions options_;
    std::shared_ptr<PipelineState> state_;
    FFmpegPuller puller_;
    std::atomic_bool running_{false};
    std::thread read_thread_;
};

} // namespace pipeline
