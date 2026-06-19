#pragma once

#include <memory>
#include <string>

#include "node/i_node.h"
#include "node/transform_node.h"
#include "pipeline/pipeline_types.h"

namespace pipeline {

class OSDNode : public runtime::INode,
                public runtime::TransformNode<InferenceMessagePtr, FrameMessage> {
public:
    explicit OSDNode(std::shared_ptr<PipelineState> state);

    bool Init() override;
    bool Start() override;
    void Stop() override;
    void Deinit() override;
    std::string Name() const override;

protected:
    void Process(InferenceMessagePtr message) override;

private:
    std::shared_ptr<PipelineState> state_;
};

} // namespace pipeline
