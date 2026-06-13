#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
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
              << "  " << exe << " --image <image.jpg>\n";
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

    return !options.image_path.empty();
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

}  // namespace

int main(int argc, char** argv) {
    LogManager::getInstance().Init();
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    DemoOptions options;
    // if (!ParseArgs(argc, argv, options)) {
    //     PrintUsage(argv[0]);
    //     return 1;
    // }

    options.model_path = DefaultModelPath();
    options.image_path = DefaultPicPath();
    std::cout << "model: " << options.model_path << "\n";
    std::cout << "image: " << options.image_path << "\n";

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
    preprocess_config.model_pixel_format = PixelFormat::kRGB24;
    preprocess_config.model_input_layout = "NCHW";
    preprocess_config.scale = 255.0f;

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
    LogManager::getInstance().FlushAll();
    return 0;
}
