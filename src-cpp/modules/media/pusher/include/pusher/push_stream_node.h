#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "node/i_node.h"
#include "node/sink_node.h"
#include "pipeline/pipeline_types.h"
#include "pusher/i_pusher.hpp"

namespace pipeline {

class PushStreamNode : public runtime::INode,
                       public runtime::SinkNode<PacketMessage> {
public:
    PushStreamNode(PipelineOptions options, std::shared_ptr<PipelineState> state);

    bool Init() override;
    bool Start() override;
    void Stop() override;
    void Deinit() override;
    std::string Name() const override;

protected:
    void Process(PacketMessage packet) override;

private:
    bool EnsureConnected();

    PipelineOptions options_;
    std::shared_ptr<PipelineState> state_;
    std::unique_ptr<IPusher> pusher_;
    std::chrono::steady_clock::time_point last_connect_attempt_;
    bool connected_{false};
};

} // namespace pipeline
