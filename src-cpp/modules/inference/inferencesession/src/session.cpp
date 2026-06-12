#include "inferencesession/session.h"

#include "common/log/logmanager.h"

bool InferenceSession::Initialize(std::shared_ptr<IModel> model,
                                  std::shared_ptr<IInferenceEngine> engine) {
    if (!model || !engine) {
        LOG_MAIN_ERROR_AT("InferenceSession requires non-null model and engine");
        return false;
    }

    model_ = std::move(model);
    engine_ = std::move(engine);

    const auto input_shape = engine_->GetInputShape();
    if (!input_shape.empty() && !model_->ConfigureInputShape(input_shape)) {
        LOG_MAIN_ERROR_AT("Failed to configure model input shape from inference engine");
        model_.reset();
        engine_.reset();
        return false;
    }

    return true;
}

FrameResult InferenceSession::Infer(const MediaFrame& frame) {
    FrameResult result;
    result.frame_id = 0;
    result.pts = static_cast<uint64_t>(frame.pts);

    if (!model_ || !engine_) {
        LOG_MAIN_ERROR_AT("InferenceSession is not initialized");
        return result;
    }

    auto input = model_->Preprocess(frame);
    TensorFrame output;
    if (!engine_->Infer(input, output)) {
        LOG_MAIN_ERROR_AT("InferenceSession engine inference failed");
        return result;
    }

    return model_->Postprocess(output);
}
