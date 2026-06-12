#include "model/yolo_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "common/log/logmanager.h"
#include "inferenceinfo/result.h"
#include "preprocessor/openvino_preprocessor.h"

namespace {

struct DetectionCandidate {
    int class_id{-1};
    float score{0.0f};
    Rectangle rect{};
};

float Area(const Rectangle& rect) {
    return std::max(0.0f, rect.width) * std::max(0.0f, rect.height);
}

float IoU(const Rectangle& a, const Rectangle& b) {
    const auto ax2 = a.x + a.width;
    const auto ay2 = a.y + a.height;
    const auto bx2 = b.x + b.width;
    const auto by2 = b.y + b.height;

    const auto ix1 = std::max(a.x, b.x);
    const auto iy1 = std::max(a.y, b.y);
    const auto ix2 = std::min(ax2, bx2);
    const auto iy2 = std::min(ay2, by2);
    const auto iw = std::max(0.0f, ix2 - ix1);
    const auto ih = std::max(0.0f, iy2 - iy1);
    const auto inter = iw * ih;
    const auto union_area = Area(a) + Area(b) - inter;

    if (union_area <= std::numeric_limits<float>::epsilon()) {
        return 0.0f;
    }
    return inter / union_area;
}

Rectangle MakeRect(float cx, float cy, float w, float h, int image_width, int image_height) {
    w = std::abs(w);
    h = std::abs(h);

    Rectangle rect;
    rect.x = std::clamp(cx - w * 0.5f, 0.0f, static_cast<float>(image_width));
    rect.y = std::clamp(cy - h * 0.5f, 0.0f, static_cast<float>(image_height));

    const auto x2 = std::clamp(cx + w * 0.5f, 0.0f, static_cast<float>(image_width));
    const auto y2 = std::clamp(cy + h * 0.5f, 0.0f, static_cast<float>(image_height));
    rect.width = std::max(0.0f, x2 - rect.x);
    rect.height = std::max(0.0f, y2 - rect.y);
    return rect;
}

const TensorPlane* FindFirstFloatTensor(const TensorFrame& output) {
    for (const auto& [_, tensor] : output.tensors_) {
        if (tensor && tensor->type == TensorType::FP32 && tensor->data && tensor->bytes > 0) {
            return tensor.get();
        }
    }
    return nullptr;
}

bool ResolveYoloShape(const TensorShape& shape,
                      size_t& count,
                      size_t& attrs,
                      bool& transposed) {
    if (shape.dims.size() == 3) {
        const auto dim1 = shape.dims[1];
        const auto dim2 = shape.dims[2];
        if (dim1 <= 0 || dim2 <= 0) {
            return false;
        }

        if (dim1 <= 256 && dim2 > dim1) {
            attrs = static_cast<size_t>(dim1);
            count = static_cast<size_t>(dim2);
            transposed = true;
        } else {
            count = static_cast<size_t>(dim1);
            attrs = static_cast<size_t>(dim2);
            transposed = false;
        }
        return true;
    }

    if (shape.dims.size() == 2) {
        const auto dim0 = shape.dims[0];
        const auto dim1 = shape.dims[1];
        if (dim0 <= 0 || dim1 <= 0) {
            return false;
        }

        if (dim0 <= 256 && dim1 > dim0) {
            attrs = static_cast<size_t>(dim0);
            count = static_cast<size_t>(dim1);
            transposed = true;
        } else {
            count = static_cast<size_t>(dim0);
            attrs = static_cast<size_t>(dim1);
            transposed = false;
        }
        return true;
    }

    return false;
}

float YoloValue(const float* data, size_t index, size_t attr, size_t count, size_t attrs, bool transposed) {
    return transposed ? data[attr * count + index] : data[index * attrs + attr];
}

bool HasObjectness(size_t attrs) {
    // Common layouts: YOLOv5 = 5 + classes, YOLOv8 = 4 + classes.
    // COCO YOLOv8 commonly has 84 attrs; COCO YOLOv5 commonly has 85 attrs.
    if (attrs == 84) {
        return false;
    }
    return attrs >= 6;
}

std::vector<DetectionCandidate> DecodeYoloTensor(const TensorPlane& tensor,
                                                 float conf_threshold,
                                                 int input_width,
                                                 int input_height) {
    std::vector<DetectionCandidate> candidates;

    size_t count = 0;
    size_t attrs = 0;
    bool transposed = false;
    if (!ResolveYoloShape(tensor.shape, count, attrs, transposed) || attrs < 5) {
        LOG_MAIN_WARN_AT("Unsupported YOLO output shape");
        return candidates;
    }

    const auto* data = tensor.Data<float>();
    const auto has_objectness = HasObjectness(attrs);
    const auto class_begin = has_objectness ? size_t{5} : size_t{4};
    if (class_begin >= attrs) {
        return candidates;
    }

    candidates.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto cx = YoloValue(data, i, 0, count, attrs, transposed);
        const auto cy = YoloValue(data, i, 1, count, attrs, transposed);
        const auto w = YoloValue(data, i, 2, count, attrs, transposed);
        const auto h = YoloValue(data, i, 3, count, attrs, transposed);
        const auto objectness = has_objectness ? YoloValue(data, i, 4, count, attrs, transposed) : 1.0f;

        int best_class = -1;
        float best_score = 0.0f;
        for (size_t c = class_begin; c < attrs; ++c) {
            const auto class_score = YoloValue(data, i, c, count, attrs, transposed);
            if (class_score > best_score) {
                best_score = class_score;
                best_class = static_cast<int>(c - class_begin);
            }
        }

        const auto score = objectness * best_score;
        if (best_class < 0 || score < conf_threshold) {
            continue;
        }

        DetectionCandidate candidate;
        candidate.class_id = best_class;
        candidate.score = score;
        candidate.rect = MakeRect(cx, cy, w, h, input_width, input_height);
        if (Area(candidate.rect) > 0.0f) {
            candidates.push_back(candidate);
        }
    }

    return candidates;
}

std::vector<DetectionCandidate> Nms(std::vector<DetectionCandidate> candidates, float nms_threshold) {
    std::sort(candidates.begin(),
              candidates.end(),
              [](const DetectionCandidate& lhs, const DetectionCandidate& rhs) {
                  return lhs.score > rhs.score;
              });

    std::vector<DetectionCandidate> kept;
    std::vector<bool> removed(candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (removed[i]) {
            continue;
        }

        kept.push_back(candidates[i]);
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (removed[j] || candidates[i].class_id != candidates[j].class_id) {
                continue;
            }
            if (IoU(candidates[i].rect, candidates[j].rect) > nms_threshold) {
                removed[j] = true;
            }
        }
    }

    return kept;
}

bool ToPositiveInt(std::size_t value, int& output) {
    if (value == 0 || value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    output = static_cast<int>(value);
    return true;
}

bool ResolveImageInputSize(const std::vector<std::size_t>& shape, int& width, int& height) {
    if (shape.size() < 2) {
        return false;
    }

    if (shape.size() == 4) {
        const auto nchw_channels = shape[1];
        if (nchw_channels == 1 || nchw_channels == 3 || nchw_channels == 4) {
            return ToPositiveInt(shape[3], width) && ToPositiveInt(shape[2], height);
        }

        const auto nhwc_channels = shape[3];
        if (nhwc_channels == 1 || nhwc_channels == 3 || nhwc_channels == 4) {
            return ToPositiveInt(shape[2], width) && ToPositiveInt(shape[1], height);
        }
    }

    return ToPositiveInt(shape[shape.size() - 1], width) &&
           ToPositiveInt(shape[shape.size() - 2], height);
}

}  // namespace

bool YoloModel::Initialize(const ModelConfig& config) {
    (void)config;

    if (!ResetPreprocessor()) {
        initialized_ = false;
        return false;
    }

    initialized_ = true;
    return true;
}

bool YoloModel::ResetPreprocessor() {
    if (input_width_ <= 0 || input_height_ <= 0) {
        LOG_MAIN_ERROR_AT("YOLO input size is invalid: {}x{}", input_width_, input_height_);
        return false;
    }

    auto preprocessor = std::make_unique<OpenVinoYoloPreprocessor>();
    OpenVinoYoloPreprocessorConfig preprocessor_config;
    preprocessor_config.input_width = static_cast<uint32_t>(input_width_);
    preprocessor_config.input_height = static_cast<uint32_t>(input_height_);

    if (!preprocessor->Initialize(preprocessor_config)) {
        return false;
    }

    preprocessor_ = std::move(preprocessor);
    return true;
}

bool YoloModel::ConfigureInputShape(const std::vector<std::size_t>& shape) {
    int width = 0;
    int height = 0;
    if (!ResolveImageInputSize(shape, width, height)) {
        LOG_MAIN_ERROR_AT("Failed to resolve YOLO input size from model shape");
        return false;
    }

    if (width == input_width_ && height == input_height_) {
        LOG_MAIN_INFO_AT("YOLO input size resolved from model shape: {}x{}", input_width_, input_height_);
        return true;
    }

    input_width_ = width;
    input_height_ = height;

    if (initialized_ && !ResetPreprocessor()) {
        initialized_ = false;
        return false;
    }

    LOG_MAIN_INFO_AT("YOLO input size resolved from model shape: {}x{}", input_width_, input_height_);
    return true;
}

TensorFrame YoloModel::Preprocess(const MediaFrame& frame) {
    if (!initialized_ || !preprocessor_) {
        LOG_MAIN_ERROR_AT("YoloModel is not initialized");
        return {};
    }
    return preprocessor_->Process(frame);
}

FrameResult YoloModel::Postprocess(const TensorFrame& output) {
    FrameResult result;
    result.frame_id = 0;
    result.pts = 0;

    const auto* tensor = FindFirstFloatTensor(output);
    if (!tensor) {
        LOG_MAIN_WARN_AT("YOLO postprocess did not find FP32 output tensor");
        return result;
    }

    auto candidates = DecodeYoloTensor(*tensor, conf_threshold_, input_width_, input_height_);
    auto detections = Nms(std::move(candidates), nms_threshold_);

    result.objects.reserve(detections.size());
    for (const auto& detection : detections) {
        ObjectResult object;
        object.object.class_id = detection.class_id;
        object.object.score = detection.score;
        object.object.rect = detection.rect;
        result.objects.push_back(std::move(object));
    }

    return result;
}
