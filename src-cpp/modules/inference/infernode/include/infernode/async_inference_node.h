#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "inferenceengine/i_engine.h"
#include "model/i_model.h"
#include "node/i_node.h"
#include "node/transform_node.h"
#include "pipeline/pipeline_types.h"

namespace pipeline {

class AsyncInferenceNode : public runtime::INode,
                           public runtime::TransformNode<FrameMessage, InferenceMessagePtr> {
public:
    AsyncInferenceNode(PipelineOptions options, std::shared_ptr<PipelineState> state);

    bool Init() override;
    bool Start() override;
    void Stop() override;
    void Deinit() override;
    std::string Name() const override;

protected:
    void Process(FrameMessage frame) override;

private:
    struct PendingGuard {
        explicit PendingGuard(AsyncInferenceNode& owner);
        ~PendingGuard();

        AsyncInferenceNode& owner;
    };

    bool EnsureReady(const MediaFrame& first_frame);
    void HandleInferResult(FrameMessage frame,
                           const InferContext& ctx,
                           InferOutput&& output);
    void FinishPending();
    void WaitPending();

    PipelineOptions options_;
    std::shared_ptr<PipelineState> state_;
    std::shared_ptr<IModel> model_;
    std::shared_ptr<IInferenceEngine> engine_;
    PixelFormat inference_pixel_format_{PixelFormat::kUnknown};
    std::atomic_bool accepting_{false};
    std::atomic<std::uint64_t> next_frame_id_{0};
    std::atomic<std::uint64_t> pending_{0};
    std::mutex op_mutex_;
    std::mutex model_mutex_;
    std::mutex emit_mutex_;
    std::mutex pending_mutex_;
    std::condition_variable pending_cv_;
    bool initialized_{false};
};

} // namespace pipeline
