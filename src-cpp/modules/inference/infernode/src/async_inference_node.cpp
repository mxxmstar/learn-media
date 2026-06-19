#include "infernode/async_inference_node.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

#include "common/log/logmanager.h"
#include "inferenceengine/openvino_engine.h"
#include "inferencesession/session.h"
#include "model/yolo_model.h"

namespace pipeline {
namespace {

bool IsSupportedInferenceFormat(PixelFormat format) {
    return format == PixelFormat::kNV12 || format == PixelFormat::kI420;
}

} // namespace

AsyncInferenceNode::PendingGuard::PendingGuard(AsyncInferenceNode& owner)
    : owner(owner) {}

AsyncInferenceNode::PendingGuard::~PendingGuard() {
    owner.FinishPending();
}

AsyncInferenceNode::AsyncInferenceNode(PipelineOptions options,
                                       std::shared_ptr<PipelineState> state)
    : options_(std::move(options)), state_(std::move(state)) {}

bool AsyncInferenceNode::Init() {
    accepting_.store(true);
    return true;
}

bool AsyncInferenceNode::Start() {
    return true;
}

void AsyncInferenceNode::Stop() {
    accepting_.store(false);

    std::shared_ptr<IInferenceEngine> engine;
    {
        std::lock_guard<std::mutex> lock(op_mutex_);
        engine = engine_;
    }

    if (engine) {
        engine->Release();
    }
    WaitPending();
}

void AsyncInferenceNode::Deinit() {
    Stop();
    std::lock_guard<std::mutex> lock(op_mutex_);
    engine_.reset();
    model_.reset();
    initialized_ = false;
    inference_pixel_format_ = PixelFormat::kUnknown;
}

std::string AsyncInferenceNode::Name() const {
    return "async_inference";
}

void AsyncInferenceNode::Process(FrameMessage frame) {
    if (!frame || !accepting_.load()) {
        return;
    }

    InferContext ctx;
    TensorFrame input;
    bool submitted = false;

    {
        std::lock_guard<std::mutex> lock(op_mutex_);
        if (!accepting_.load()) {
            return;
        }
        if (!EnsureReady(*frame)) {
            state_->stats.inference_errors.fetch_add(1);
            return;
        }
        if (frame->pixel_format != inference_pixel_format_) {
            LOG_MAIN_ERROR_AT("frame pixel format changed after inference engine init");
            state_->stats.inference_errors.fetch_add(1);
            return;
        }

        {
            std::lock_guard<std::mutex> model_lock(model_mutex_);
            input = model_->Preprocess(*frame);
        }
        if (input.Empty()) {
            state_->stats.inference_errors.fetch_add(1);
            LOG_MAIN_ERROR_AT("preprocess produced empty tensor frame");
            return;
        }

        ctx.frame_id = next_frame_id_.fetch_add(1);
        ctx.pts = static_cast<std::uint64_t>(frame->pts);
        pending_.fetch_add(1);
        submitted = engine_->InferAsync(
            ctx,
            input,
            [this, frame = std::move(frame)](const InferContext& done_ctx,
                                             InferOutput&& output) mutable {
                HandleInferResult(std::move(frame), done_ctx, std::move(output));
            });
    }

    if (!submitted) {
        FinishPending();
        state_->stats.inference_errors.fetch_add(1);
        return;
    }

    state_->stats.inference_submitted.fetch_add(1);
}

bool AsyncInferenceNode::EnsureReady(const MediaFrame& first_frame) {
    if (initialized_) {
        return true;
    }

    if (!IsSupportedInferenceFormat(first_frame.pixel_format)) {
        LOG_MAIN_ERROR_AT("unsupported inference pixel format: {}",
                          static_cast<int>(first_frame.pixel_format));
        return false;
    }
    if (options_.model_path.empty() || !std::filesystem::exists(options_.model_path)) {
        LOG_MAIN_ERROR_AT("model path does not exist: {}", options_.model_path);
        return false;
    }

    auto model = std::make_shared<YoloModel>();
    ModelConfig model_config;
    model_config.name = options_.model_name;
    model_config.class_count = options_.class_count;
    if (!model->Initialize(model_config)) {
        LOG_MAIN_ERROR_AT("initialize YOLO model failed");
        return false;
    }

    auto engine = std::make_shared<OpenVinoCpuEngine>();
    OpenVinoPreprocessConfig preprocess_config;
    preprocess_config.enabled = true;
    preprocess_config.input_pixel_format = first_frame.pixel_format;
    preprocess_config.model_pixel_format = options_.model_pixel_format;
    preprocess_config.model_input_layout = options_.model_input_layout;
    preprocess_config.scale = options_.preprocess_scale;

    EngineLoadConfig load_config;
    load_config.engine.model_path = options_.model_path;
    load_config.engine.backend = "OPENVINO";
    load_config.engine.device = "CPU";
    load_config.engine.request_count = std::max<std::uint32_t>(1, options_.infer_request_count);
    load_config.engine.support_async = true;
    load_config.preprocess = preprocess_config;

    if (!engine->LoadModel(load_config)) {
        LOG_MAIN_ERROR_AT("load inference engine failed");
        return false;
    }
    if (!HasEngineCapability(engine->Supports(), EngineCapability::ASYNC)) {
        LOG_MAIN_ERROR_AT("inference engine does not support async inference");
        engine->Release();
        return false;
    }

    InferenceSession initializer;
    if (!initializer.Initialize(model, engine)) {
        LOG_MAIN_ERROR_AT("initialize inference session metadata failed");
        engine->Release();
        return false;
    }

    model_ = std::move(model);
    engine_ = std::move(engine);
    inference_pixel_format_ = first_frame.pixel_format;
    initialized_ = true;
    LOG_MAIN_INFO_AT("async inference ready: model={}, requests={}",
                     options_.model_path,
                     load_config.engine.request_count);
    return true;
}

void AsyncInferenceNode::HandleInferResult(FrameMessage frame,
                                           const InferContext& ctx,
                                           InferOutput&& output) {
    PendingGuard guard(*this);
    if (!output.success) {
        state_->stats.inference_errors.fetch_add(1);
        return;
    }

    FrameResult result{};
    try {
        std::lock_guard<std::mutex> lock(model_mutex_);
        if (!model_) {
            state_->stats.inference_errors.fetch_add(1);
            return;
        }
        result = model_->Postprocess(output.output);
    } catch (const std::exception& e) {
        state_->stats.inference_errors.fetch_add(1);
        LOG_MAIN_ERROR_AT("postprocess failed: {}", e.what());
        return;
    }

    result.frame_id = ctx.frame_id;
    result.pts = ctx.pts;
    state_->stats.inferred_frames.fetch_add(1);
    state_->stats.detected_objects.fetch_add(result.objects.size());

    if (!accepting_.load()) {
        return;
    }

    auto message = std::make_shared<InferenceMessage>();
    message->frame = std::move(frame);
    message->result = std::move(result);
    std::lock_guard<std::mutex> emit_lock(emit_mutex_);
    Emit(std::move(message));
}

void AsyncInferenceNode::FinishPending() {
    const auto previous = pending_.fetch_sub(1);
    if (previous <= 1) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_cv_.notify_all();
    }
}

void AsyncInferenceNode::WaitPending() {
    std::unique_lock<std::mutex> lock(pending_mutex_);
    pending_cv_.wait(lock, [this]() {
        return pending_.load() == 0;
    });
}

} // namespace pipeline
