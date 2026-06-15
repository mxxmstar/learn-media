#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/utils/logger.hpp>

#include "common/log/logmanager.h"
#include "defines/media_frame.hpp"
#include "inferenceengine/openvino_engine.h"
#include "inferencesession/session.h"
#include "model/yolo_model.h"
#include "osd_batch.h"
#include "osd_color.h"
#include "yuv420_osdrender.h"

namespace {

class VectorMediaBuffer : public IMediaBuffer {
public:
    explicit VectorMediaBuffer(std::vector<uint8_t> data)
        : data_(std::move(data)) {
    }

    uint8_t* Data() override {
        return data_.data();
    }

    const uint8_t* Data() const override {
        return data_.data();
    }

    size_t Size() const override {
        return data_.size();
    }

private:
    std::vector<uint8_t> data_;
};

struct DemoOptions {
    std::string model_path;
    std::string image_path;
    std::string output_path;
};

std::string DefaultModelPath() {
    return (std::filesystem::path(__FILE__).parent_path() /
            "yolov5_model" /
            "yolov5s.xml").string();
}

std::string DefaultPicPath() {
    return (std::filesystem::path(__FILE__).parent_path() /
            "person.png").string();
}

void PrintUsage(const char* exe) {
    std::cout << "Usage:\n"
              << "  " << exe << " [--image <image.jpg>] [--output <result.png>]\n";
    std::cout << "  optional: --model <model.xml|model.onnx>\n";
}

bool ParseArgs(int argc, char** argv, DemoOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const std::string& name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--model") {
            if (const auto* value = require_value(arg)) {
                options.model_path = value;
            } else {
                return false;
            }
        } else if (arg == "--image") {
            if (const auto* value = require_value(arg)) {
                options.image_path = value;
            } else {
                return false;
            }
        } else if (arg == "--output") {
            if (const auto* value = require_value(arg)) {
                options.output_path = value;
            } else {
                return false;
            }
        } else if (arg == "--help" || arg == "-h") {
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }

    if (options.model_path.empty()) {
        options.model_path = DefaultModelPath();
    }
    if (options.image_path.empty()) {
        options.image_path = DefaultPicPath();
    }
    if (options.output_path.empty()) {
        const std::filesystem::path image_path(options.image_path);
        options.output_path = (image_path.parent_path() /
            (image_path.stem().string() + "_osd.png")).string();
    }

    return true;
}

std::vector<uint8_t> MatToVector(const cv::Mat& mat) {
    const auto bytes = mat.total() * mat.elemSize();
    std::vector<uint8_t> data(bytes);
    if (bytes > 0) {
        std::memcpy(data.data(), mat.data, bytes);
    }
    return data;
}

bool LoadImageAsI420Frame(const std::string& image_path, MediaFrame& frame) {
    auto bgr = cv::imread(image_path, cv::IMREAD_COLOR);
    if (bgr.empty()) {
        std::cerr << "Failed to read image: " << image_path << "\n";
        return false;
    }

    const int even_width = bgr.cols & ~1;
    const int even_height = bgr.rows & ~1;
    if (even_width <= 0 || even_height <= 0) {
        std::cerr << "Image is too small for YUV420: " << bgr.cols << "x" << bgr.rows << "\n";
        return false;
    }
    if (even_width != bgr.cols || even_height != bgr.rows) {
        bgr = bgr(cv::Rect(0, 0, even_width, even_height)).clone();
    }

    cv::Mat i420;
    cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);
    auto buffer = std::make_shared<VectorMediaBuffer>(MatToVector(i420));

    frame = MediaFrame{};
    frame.type = MediaType::VIDEO;
    frame.pixel_format = PixelFormat::kI420;
    frame.width = even_width;
    frame.height = even_height;
    frame.plane_count = 3;
    frame.stride[0] = even_width;
    frame.stride[1] = even_width / 2;
    frame.stride[2] = even_width / 2;
    frame.plane_offset[0] = 0;
    frame.plane_offset[1] = even_width * even_height;
    frame.plane_offset[2] = frame.plane_offset[1] + even_width * even_height / 4;
    frame.buffer = std::move(buffer);
    return true;
}

void PrintResult(const FrameResult& result) {
    std::cout << "detections: " << result.objects.size() << "\n";
    for (size_t i = 0; i < result.objects.size(); ++i) {
        const auto& object = result.objects[i].object;
        std::cout << "#" << i
                  << " class_id=" << object.class_id
                  << " score=" << object.score
                  << " x=" << object.rect.x
                  << " y=" << object.rect.y
                  << " width=" << object.rect.width
                  << " height=" << object.rect.height
                  << "\n";
    }
}

YuvColor ColorForClass(int class_id) {
    switch ((class_id % 4 + 4) % 4) {
        case 0:
            return OSDColor::Red;
        case 1:
            return OSDColor::Green;
        case 2:
            return OSDColor::Blue;
        default:
            return OSDColor::White;
    }
}

std::string FormatObjectLabel(const ObjectMeta& object) {
    std::ostringstream oss;
    oss << "c" << object.class_id << " "
        << std::fixed << std::setprecision(2) << object.score;
    return oss.str();
}

OverlayBatch BuildOverlayBatch(const FrameResult& result, const MediaFrame& frame) {
    OverlayBatch batch;
    const int thickness = std::max(2, std::min(frame.width, frame.height) / 320);

    for (const auto& object_result : result.objects) {
        const auto& object = object_result.object;
        const auto& rect = object.rect;
        const int x1 = std::clamp(static_cast<int>(std::floor(rect.x)), 0, frame.width - 1);
        const int y1 = std::clamp(static_cast<int>(std::floor(rect.y)), 0, frame.height - 1);
        const int x2 = std::clamp(static_cast<int>(std::ceil(rect.x + rect.width)), 0, frame.width);
        const int y2 = std::clamp(static_cast<int>(std::ceil(rect.y + rect.height)), 0, frame.height);
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        const auto color = ColorForClass(object.class_id);
        auto overlay = std::make_shared<OverlayRect>();
        overlay->x = x1;
        overlay->y = y1;
        overlay->width = x2 - x1;
        overlay->height = y2 - y1;
        overlay->thickness = thickness;
        overlay->color = color;
        batch.Add(std::move(overlay));

        auto label = std::make_shared<OverlayText>();
        label->text = FormatObjectLabel(object);
        label->scale = 1;
        label->char_spacing = 1;
        label->color = color;
        label->draw_background = true;
        label->background_padding = 2;
        label->background_color = OSDColor::Black;
        label->x = x1;

        const int text_height = 16 * label->scale;
        const int label_height = text_height + label->background_padding * 2;
        const int preferred_y = y1 >= label_height ? y1 - label_height : y1 + thickness;
        label->y = std::clamp(preferred_y, 0, std::max(0, frame.height - text_height));
        batch.Add(std::move(label));
    }

    return batch;
}

bool DrawDetections(MediaFrame& frame, const FrameResult& result) {
    auto batch = BuildOverlayBatch(result, frame);
    if (batch.Empty()) {
        return true;
    }

    Yuv420Renderer renderer;
    return renderer.Draw(frame, batch);
}

bool CopyI420ToCompactBuffer(const MediaFrame& frame, std::vector<uint8_t>& compact) {
    if (frame.pixel_format != PixelFormat::kI420 || !frame.buffer || !frame.buffer->Data()) {
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    const int chroma_width = (frame.width + 1) / 2;
    const int chroma_height = (frame.height + 1) / 2;
    const int y_stride = frame.stride[0] > 0 ? frame.stride[0] : frame.width;
    const int u_stride = frame.stride[1] > 0 ? frame.stride[1] : chroma_width;
    const int v_stride = frame.stride[2] > 0 ? frame.stride[2] : chroma_width;
    if (y_stride < frame.width || u_stride < chroma_width || v_stride < chroma_width) {
        return false;
    }

    const size_t y_size = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    const size_t uv_size = static_cast<size_t>(chroma_width) * static_cast<size_t>(chroma_height);
    compact.assign(y_size + uv_size * 2, 0);

    const auto* src = frame.buffer->Data();
    auto* dst = compact.data();
    for (int row = 0; row < frame.height; ++row) {
        std::memcpy(
            dst + static_cast<size_t>(row) * static_cast<size_t>(frame.width),
            src + static_cast<size_t>(frame.plane_offset[0])
                + static_cast<size_t>(row) * static_cast<size_t>(y_stride),
            static_cast<size_t>(frame.width)
        );
    }

    auto* dst_u = dst + y_size;
    auto* dst_v = dst_u + uv_size;
    for (int row = 0; row < chroma_height; ++row) {
        std::memcpy(
            dst_u + static_cast<size_t>(row) * static_cast<size_t>(chroma_width),
            src + static_cast<size_t>(frame.plane_offset[1])
                + static_cast<size_t>(row) * static_cast<size_t>(u_stride),
            static_cast<size_t>(chroma_width)
        );
        std::memcpy(
            dst_v + static_cast<size_t>(row) * static_cast<size_t>(chroma_width),
            src + static_cast<size_t>(frame.plane_offset[2])
                + static_cast<size_t>(row) * static_cast<size_t>(v_stride),
            static_cast<size_t>(chroma_width)
        );
    }

    return true;
}

bool SaveI420FrameAsImage(const MediaFrame& frame, const std::string& output_path) {
    std::vector<uint8_t> compact;
    if (!CopyI420ToCompactBuffer(frame, compact)) {
        std::cerr << "Failed to prepare I420 frame for image output\n";
        return false;
    }

    cv::Mat yuv(frame.height * 3 / 2, frame.width, CV_8UC1, compact.data());
    cv::Mat bgr;
    cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_I420);

    const std::filesystem::path path(output_path);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    if (!cv::imwrite(output_path, bgr)) {
        std::cerr << "Failed to write output image: " << output_path << "\n";
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    LogManager::getInstance().Init();
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    DemoOptions options;
    if (!ParseArgs(argc, argv, options)) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::cout << "model: " << options.model_path << "\n";
    std::cout << "image: " << options.image_path << "\n";
    std::cout << "output: " << options.output_path << "\n";

    MediaFrame frame;
    if (!LoadImageAsI420Frame(options.image_path, frame)) {
        return 2;
    }

    ModelConfig model_config;
    model_config.name = "yolo";

    auto model = std::make_shared<YoloModel>();
    if (!model->Initialize(model_config)) {
        std::cerr << "Failed to initialize YoloModel\n";
        return 3;
    }

    auto engine = std::make_shared<OpenVinoCpuEngine>();
    OpenVinoPreprocessConfig preprocess_config;
    preprocess_config.enabled = true;
    preprocess_config.input_pixel_format = PixelFormat::kI420;

    EngineLoadConfig load_config;
    load_config.engine.model_path = options.model_path;
    load_config.engine.backend = "OPENVINO";
    load_config.engine.device = "CPU";
    load_config.engine.request_count = 1;
    load_config.preprocess = preprocess_config;

    if (!engine->LoadModel(load_config)) {
        std::cerr << "Failed to load model: " << options.model_path << "\n";
        return 4;
    }

    InferenceSession session;
    if (!session.Initialize(model, engine)) {
        std::cerr << "Failed to initialize inference session\n";
        return 5;
    }

    const auto result = session.Infer(frame);
    PrintResult(result);
    if (!DrawDetections(frame, result)) {
        std::cerr << "Failed to draw detection boxes on frame\n";
        return 6;
    }
    if (!SaveI420FrameAsImage(frame, options.output_path)) {
        return 7;
    }

    std::cout << "saved OSD image: " << options.output_path << "\n";
    LogManager::getInstance().FlushAll();
    return 0;
}
