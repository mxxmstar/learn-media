#include "inferenceengine/openvino_engine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#include <openvino/core/layout.hpp>
#include <openvino/core/preprocess/pre_post_process.hpp>
#include <openvino/core/preprocess/resize_algorithm.hpp>

#include "common/log/logmanager.h"

namespace {

constexpr auto kCpuDevice = "CPU";

size_t TensorTypeSize(TensorType type) {
    switch (type) {
        case TensorType::FP32:
        case TensorType::INT32:
            return 4;
        case TensorType::FP16:
            return 2;
        case TensorType::INT8:
        case TensorType::UINT8:
        case TensorType::BOOL:
            return 1;
        case TensorType::INT64:
            return 8;
        case TensorType::UNKNOWN:
        default:
            return 0;
    }
}

TensorType FromOvElementType(const ov::element::Type& type) {
    if (type == ov::element::f32) {
        return TensorType::FP32;
    }
    if (type == ov::element::f16) {
        return TensorType::FP16;
    }
    if (type == ov::element::i8) {
        return TensorType::INT8;
    }
    if (type == ov::element::u8) {
        return TensorType::UINT8;
    }
    if (type == ov::element::i32) {
        return TensorType::INT32;
    }
    if (type == ov::element::i64) {
        return TensorType::INT64;
    }
    if (type == ov::element::boolean) {
        return TensorType::BOOL;
    }
    return TensorType::UNKNOWN;
}

ov::element::Type ToOvElementType(TensorType type) {
    switch (type) {
        case TensorType::FP32:
            return ov::element::f32;
        case TensorType::FP16:
            return ov::element::f16;
        case TensorType::INT8:
            return ov::element::i8;
        case TensorType::UINT8:
            return ov::element::u8;
        case TensorType::INT32:
            return ov::element::i32;
        case TensorType::INT64:
            return ov::element::i64;
        case TensorType::BOOL:
            return ov::element::boolean;
        case TensorType::UNKNOWN:
        default:
            throw std::runtime_error("Unsupported TensorType");
    }
}

bool IsYuvFormat(PixelFormat format) {
    return format == PixelFormat::kNV12 || format == PixelFormat::kI420;
}

ov::preprocess::ColorFormat ToInputColorFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::kNV12:
            return ov::preprocess::ColorFormat::NV12_TWO_PLANES;
        case PixelFormat::kI420:
            return ov::preprocess::ColorFormat::I420_THREE_PLANES;
        case PixelFormat::kBGR24:
            return ov::preprocess::ColorFormat::BGR;
        case PixelFormat::kRGB24:
            return ov::preprocess::ColorFormat::RGB;
        case PixelFormat::kGRAY8:
            return ov::preprocess::ColorFormat::GRAY;
        case PixelFormat::kNV21:
        case PixelFormat::kUnknown:
        default:
            throw std::runtime_error("Unsupported OpenVINO preprocess input pixel format");
    }
}

ov::preprocess::ColorFormat ToModelColorFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::kBGR24:
            return ov::preprocess::ColorFormat::BGR;
        case PixelFormat::kRGB24:
            return ov::preprocess::ColorFormat::RGB;
        case PixelFormat::kGRAY8:
            return ov::preprocess::ColorFormat::GRAY;
        default:
            throw std::runtime_error("Unsupported OpenVINO preprocess model pixel format");
    }
}

std::shared_ptr<ov::Model> ApplyOpenVinoPreprocess(std::shared_ptr<ov::Model> model,
                                                   const OpenVinoPreprocessConfig& config) {
    if (!config.enabled) {
        return model;
    }

    if (model->inputs().size() != 1) {
        throw std::runtime_error("OpenVINO internal image preprocessing requires a single model input");
    }

    const auto model_element_type = model->input().get_element_type();
    ov::preprocess::PrePostProcessor processor(model);
    auto& input = processor.input();
    auto& tensor = input.tensor();

    tensor.set_element_type(ov::element::u8);
    tensor.set_spatial_dynamic_shape();

    switch (config.input_pixel_format) {
        case PixelFormat::kNV12:
            tensor.set_color_format(ov::preprocess::ColorFormat::NV12_TWO_PLANES, {"y", "uv"});
            break;
        case PixelFormat::kI420:
            tensor.set_color_format(ov::preprocess::ColorFormat::I420_THREE_PLANES, {"y", "u", "v"});
            break;
        case PixelFormat::kBGR24:
        case PixelFormat::kRGB24:
        case PixelFormat::kGRAY8:
            tensor.set_layout("NHWC");
            tensor.set_color_format(ToInputColorFormat(config.input_pixel_format));
            break;
        default:
            throw std::runtime_error("Unsupported OpenVINO internal preprocess input format");
    }

    if (!config.model_input_layout.empty()) {
        input.model().set_layout(ov::Layout(config.model_input_layout));
    }

    auto& preprocess = input.preprocess();
    if (IsYuvFormat(config.input_pixel_format) ||
        config.input_pixel_format != config.model_pixel_format) {
        preprocess.convert_color(ToModelColorFormat(config.model_pixel_format));
    }
    preprocess.resize(ov::preprocess::ResizeAlgorithm::RESIZE_LINEAR);
    preprocess.convert_element_type(model_element_type);
    if (config.scale > 0.0f) {
        preprocess.scale(config.scale);
    }

    return processor.build();
}

TensorShape FromOvShape(const ov::Shape& shape) {
    TensorShape result;
    result.dims.reserve(shape.size());
    for (auto dim : shape) {
        if (dim > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
            throw std::runtime_error("OpenVINO shape dimension is too large");
        }
        result.dims.push_back(static_cast<int64_t>(dim));
    }
    return result;
}

std::vector<std::size_t> StaticShapeFromPartialShape(const ov::PartialShape& shape) {
    std::vector<std::size_t> result;
    if (shape.rank().is_dynamic()) {
        return result;
    }

    result.reserve(shape.rank().get_length());
    for (const auto& dim : shape) {
        if (!dim.is_static()) {
            result.clear();
            return result;
        }
        result.push_back(static_cast<std::size_t>(dim.get_length()));
    }

    return result;
}

ov::Shape ToOvShape(const TensorShape& shape) {
    ov::Shape result;
    result.reserve(shape.dims.size());
    for (auto dim : shape.dims) {
        if (dim < 0) {
            throw std::runtime_error("Input tensor contains dynamic dimension");
        }
        result.push_back(static_cast<size_t>(dim));
    }
    return result;
}

std::string PortName(const ov::Output<const ov::Node>& port,
                     size_t index,
                     bool is_input) {
    const auto& names = port.get_names();
    if (!names.empty()) {
        return *names.begin();
    }

    try {
        return port.get_any_name();
    } catch (const std::exception&) {
        return std::string(is_input ? "input_" : "output_") + std::to_string(index);
    }
}

TensorShape FromPartialShape(const ov::PartialShape& shape) {
    TensorShape result;
    if (shape.rank().is_dynamic()) {
        return result;
    }

    result.dims.reserve(shape.rank().get_length());
    for (const auto& dim : shape) {
        result.dims.push_back(dim.is_static() ? dim.get_length() : -1);
    }
    return result;
}

TensorDesc BuildTensorDesc(const ov::Output<const ov::Node>& port,
                           size_t index,
                           bool is_input) {
    TensorDesc desc;
    desc.name = PortName(port, index, is_input);
    desc.type = FromOvElementType(port.get_element_type());
    desc.shape = FromPartialShape(port.get_partial_shape());
    desc.dynamic_shape = port.get_partial_shape().is_dynamic();
    desc.is_input = is_input;

    if (!desc.dynamic_shape) {
        desc.element_count = desc.shape.ElementCount();
        desc.bytes = desc.element_count * TensorTypeSize(desc.type);
    }

    return desc;
}

const TensorPlane* FindInputTensor(const TensorFrame& frame,
                                  const ov::Output<const ov::Node>& port,
                                  size_t index,
                                  size_t total_inputs) {
    for (const auto& name : port.get_names()) {
        if (const auto* tensor = frame.FindTensor(name)) {
            return tensor;
        }
    }

    try {
        if (const auto* tensor = frame.FindTensor(port.get_any_name())) {
            return tensor;
        }
    } catch (const std::exception&) {
    }

    const auto fallback = std::string("input_") + std::to_string(index);
    if (const auto* tensor = frame.FindTensor(fallback)) {
        return tensor;
    }

    if (total_inputs == 2) {
        const char* plane_names[] = {"y", "uv"};
        if (index < 2) {
            if (const auto* tensor = frame.FindTensor(plane_names[index])) {
                return tensor;
            }
        }
    }

    if (total_inputs == 3) {
        const char* plane_names[] = {"y", "u", "v"};
        if (index < 3) {
            if (const auto* tensor = frame.FindTensor(plane_names[index])) {
                return tensor;
            }
        }
    }

    if (total_inputs == 1 && frame.Size() == 1) {
        return frame.FirstTensor();
    }

    return nullptr;
}

/// @brief 绑定推理请求的输入张量
/// 将 TensorFrame 中的数据绑定到 OpenVINO 推理请求的输入端口
/// @param request 推理请求
/// @param compiled_model 编译后的模型，用于获取输入端口信息
/// @param input 待绑定的输入张量包
void BindInputs(ov::InferRequest& request,
                const ov::CompiledModel& compiled_model,
                const TensorFrame& input) {
    const auto inputs = compiled_model.inputs();
    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& port = inputs[i];
        const auto* tensor = FindInputTensor(input, port, i, inputs.size());
        if (!tensor) {
            throw std::runtime_error("Missing input tensor: " + PortName(port, i, true));
        }
        if (!tensor->data || tensor->bytes == 0) {
            throw std::runtime_error("Input tensor has empty data: " + tensor->name);
        }

        const auto ov_type = ToOvElementType(tensor->type);
        const auto ov_shape = ToOvShape(tensor->shape);
        ov::Tensor ov_tensor(ov_type, ov_shape, tensor->data);

        if (ov_tensor.get_byte_size() > tensor->bytes) {
            throw std::runtime_error("Input tensor buffer is smaller than shape requires: " + tensor->name);
        }

        request.set_tensor(port, ov_tensor);
    }
}

TensorFrame CollectOutputs(ov::InferRequest& request,
                             const ov::CompiledModel& compiled_model) {
    TensorFrame output;
    const auto outputs = compiled_model.outputs();
    for (size_t i = 0; i < outputs.size(); ++i) {
        const auto& port = outputs[i];
        auto ov_tensor = request.get_tensor(port);
        const auto bytes = ov_tensor.get_byte_size();
        auto buffer = std::make_shared<CpuTensorBuffer>(bytes);

        if (bytes > 0 && ov_tensor.data()) {
            std::memcpy(buffer->Data(), ov_tensor.data(), bytes);
        }

        auto tensor = std::make_unique<TensorPlane>();
        tensor->name = PortName(port, i, false);
        tensor->type = FromOvElementType(ov_tensor.get_element_type());
        tensor->shape = FromOvShape(ov_tensor.get_shape());
        tensor->SetBuffer(std::move(buffer), TensorMemoryType::OPENVINO_CPU);
        output.AddTensor(std::move(tensor));
    }

    return output;
}

class RequestLease {
public:
    RequestLease(OpenVinoInferRequestPool* pool, std::shared_ptr<ov::InferRequest> request)
        : pool_(pool),
          request_(std::move(request)) {
    }

    ~RequestLease() {
        if (pool_ && request_) {
            pool_->Release(std::move(request_));
        }
    }

    ov::InferRequest& operator*() {
        return *request_;
    }

    ov::InferRequest* operator->() {
        return request_.get();
    }

private:
    OpenVinoInferRequestPool* pool_{nullptr};
    std::shared_ptr<ov::InferRequest> request_;
};

}  // namespace

OpenVinoCpuEngine::OpenVinoCpuEngine()
    : request_pool_(std::make_unique<OpenVinoInferRequestPool>()) {
}

OpenVinoCpuEngine::~OpenVinoCpuEngine() {
    Release();
}

bool OpenVinoCpuEngine::LoadModel(const ModelConfig& config) {
    Release();

    if (config.path.empty()) {
        LOG_MAIN_ERROR_AT("OpenVINO model path is empty");
        return false;
    }

    if (!std::filesystem::exists(config.path)) {
        LOG_MAIN_ERROR_AT("OpenVINO model path does not exist: {}", config.path);
        return false;
    }

    try {
        config_ = config;
        config_.backend = "OPENVINO";
        config_.device = kCpuDevice;
        config_.request_count = std::max<uint32_t>(1, config.request_count);

        auto raw_model = core_.read_model(config_.path);
        model_input_shape_.clear();
        if (!raw_model->inputs().empty()) {
            model_input_shape_ = StaticShapeFromPartialShape(raw_model->input().get_partial_shape());
        }

        auto model = ApplyOpenVinoPreprocess(std::move(raw_model), preprocess_config_);
        compiled_model_ = core_.compile_model(model, kCpuDevice);
        if (model_input_shape_.empty() && !compiled_model_.inputs().empty()) {
            model_input_shape_ = StaticShapeFromPartialShape(compiled_model_.input().get_partial_shape());
        }

        request_pool_ = std::make_unique<OpenVinoInferRequestPool>();
        if (!request_pool_->Initialize(compiled_model_, config_.request_count)) {
            compiled_model_ = {};
            model_input_shape_.clear();
            return false;
        }

        initialized_ = true;
        accepting_async_.store(true, std::memory_order_release);
        LOG_MAIN_INFO_AT("Loaded OpenVINO CPU model: {}", config_.path);
        return true;
    } catch (const std::exception& e) {
        initialized_ = false;
        accepting_async_.store(false, std::memory_order_release);
        compiled_model_ = {};
        model_input_shape_.clear();
        LOG_MAIN_ERROR_AT("Failed to load OpenVINO CPU model {}: {}", config.path, e.what());
        return false;
    }
}

void OpenVinoCpuEngine::SetPreprocessConfig(const OpenVinoPreprocessConfig& config) {
    preprocess_config_ = config;
}

bool OpenVinoCpuEngine::Infer(const TensorFrame& input, TensorFrame& output) {
    if (!initialized_ || !request_pool_) {
        LOG_MAIN_ERROR_AT("OpenVINO CPU engine is not initialized");
        return false;
    }

    auto request = request_pool_->Acquire();
    RequestLease lease(request_pool_.get(), std::move(request));

    try {
        BindInputs(*lease, compiled_model_, input);
        lease->infer();
        output = CollectOutputs(*lease, compiled_model_);
        return true;
    } catch (const std::exception& e) {
        LOG_MAIN_ERROR_AT("OpenVINO CPU inference failed: {}", e.what());
        return false;
    }
}

bool OpenVinoCpuEngine::InferAsync(const InferContext& ctx,
                                   const TensorFrame& input,
                                   InferCallback cb) {
    if (!initialized_ || !request_pool_) {
        LOG_MAIN_ERROR_AT("OpenVINO CPU engine is not initialized");
        return false;
    }
    if (!accepting_async_.load(std::memory_order_acquire)) {
        LOG_MAIN_WARN_AT("OpenVINO CPU engine is not accepting async inference");
        return false;
    }

    auto input_copy = std::make_shared<TensorFrame>(input);
    auto request = request_pool_->Acquire();

    try {
        BindInputs(*request, compiled_model_, *input_copy);
        request->set_callback(
            [this,
             request,
             input_copy,
             ctx,
             cb = std::move(cb)](std::exception_ptr ex) mutable {
                TensorFrame output;

                try {
                    if (ex) {
                        std::rethrow_exception(ex);
                    }
                    output = CollectOutputs(*request, compiled_model_);
                } catch (const std::exception& e) {
                    LOG_MAIN_ERROR_AT("OpenVINO CPU async inference failed: {}", e.what());
                }

                try {
                    if (cb) {
                        cb(ctx, std::move(output));
                    }
                } catch (const std::exception& e) {
                    LOG_MAIN_ERROR_AT("OpenVINO CPU async callback failed: {}", e.what());
                }

                if (request_pool_) {
                    request_pool_->Release(std::move(request));
                }
            });
        request->start_async();
        return true;
    } catch (const std::exception& e) {
        LOG_MAIN_ERROR_AT("Failed to start OpenVINO CPU async inference: {}", e.what());
        request_pool_->Release(std::move(request));
        return false;
    }
}

std::vector<std::size_t> OpenVinoCpuEngine::GetInputShape() const {
    if (!initialized_) {
        return {};
    }

    return model_input_shape_;
}

void OpenVinoCpuEngine::Release() {
    accepting_async_.store(false, std::memory_order_release);

    if (initialized_) {
        WaitAll();
    }

    request_pool_.reset();
    compiled_model_ = {};
    model_input_shape_.clear();
    initialized_ = false;
}

bool OpenVinoCpuEngine::WaitAll() {
    if (!request_pool_) {
        return true;
    }

    while (request_pool_->IdleCount() < request_pool_->TotalCount()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return true;
}

std::vector<TensorDesc> OpenVinoCpuEngine::GetInputDesc() const {
    std::vector<TensorDesc> descs;
    if (!initialized_) {
        return descs;
    }

    const auto inputs = compiled_model_.inputs();
    descs.reserve(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        descs.push_back(BuildTensorDesc(inputs[i], i, true));
    }
    return descs;
}

std::vector<TensorDesc> OpenVinoCpuEngine::GetOutputDesc() const {
    std::vector<TensorDesc> descs;
    if (!initialized_) {
        return descs;
    }

    const auto outputs = compiled_model_.outputs();
    descs.reserve(outputs.size());
    for (size_t i = 0; i < outputs.size(); ++i) {
        descs.push_back(BuildTensorDesc(outputs[i], i, false));
    }
    return descs;
}

EngineInfo OpenVinoCpuEngine::GetEngineInfo() const {
    EngineInfo info;
    info.backend = "OPENVINO";
    info.device = kCpuDevice;
    info.support_async = true;
    info.max_batch_size = static_cast<uint32_t>(std::max(1, config_.batch_size));

    for (const auto& desc : GetInputDesc()) {
        if (desc.dynamic_shape) {
            info.support_dynamic_shape = true;
            break;
        }
    }

    return info;
}

TensorMemoryType OpenVinoCpuEngine::GetMemoryType() const {
    return TensorMemoryType::OPENVINO_CPU;
}
