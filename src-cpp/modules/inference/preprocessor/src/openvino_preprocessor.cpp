#include "preprocessor/openvino_preprocessor.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "common/log/logmanager.h"

namespace {
/// @brief 媒体帧平面张量缓冲区（适配多种格式）
class MediaFramePlaneTensorBuffer : public TensorBuffer {
public:
    MediaFramePlaneTensorBuffer(std::shared_ptr<IMediaBuffer> media_buffer,
                                size_t offset,
                                size_t bytes)
        : media_buffer_(std::move(media_buffer)),
          offset_(offset),
          bytes_(bytes) {
    }

    void* Data() override {
        return media_buffer_ ? media_buffer_->Data() + offset_ : nullptr;
    }

    const void* Data() const override {
        return media_buffer_ ? media_buffer_->Data() + offset_ : nullptr;
    }

    size_t Size() const override {
        return bytes_;
    }

private:
    std::shared_ptr<IMediaBuffer> media_buffer_;
    size_t offset_{0};
    size_t bytes_{0};
};

/// @brief 获取图像平面的偏移量
/// @param frame 输入的原始 MediaFrame（包含buffer、尺寸、stride等信息）
/// @param index 平面索引
/// @param fallback 默认值
/// @return 平面数据在buffer中的偏移量
size_t PlaneOffset(const MediaFrame& frame, int index, size_t fallback) {
    if (frame.plane_count > index && frame.plane_offset[index] > 0) {
        return static_cast<size_t>(frame.plane_offset[index]);
    }
    return fallback;
}

/// @brief 获取图像平面的跨度
/// @param frame 输入的原始 MediaFrame（包含buffer、尺寸、stride等信息）
/// @param index 平面索引
/// @param fallback 默认值
int32_t PlaneStride(const MediaFrame& frame, int index, int32_t fallback) {
    if (frame.stride[index] > 0) {
        return frame.stride[index];
    }
    return fallback;
}

/// @brief 根据输入的图像平面参数，安全地指向/创建一个 TensorBuffer
/// 上层自己提供offset，stride等，防止mediaframe中的数据不完整
/// 从 buffer 的 offset 位置，取 rows 行数据，每行有效长度 row_bytes，实际步长 stride_bytes。
/// 如果无 padding 则零拷贝共享，有 padding 则逐行去 padding 拷贝
/// @param frame 输入的原始 MediaFrame（包含buffer、尺寸、stride等信息）
/// @param offset 当前平面数据在buffer中的偏移量
/// @param rows 当前平面的行数
/// @param row_bytes 当前平面每行的字节数
/// @param stride_bytes 当前平面的行跨度（字节数）
/// @return 创建的 TensorBuffer 指针，或 nullptr 如果参数无效
std::shared_ptr<TensorBuffer> MakePlaneBuffer(const MediaFrame& frame,
                                              size_t offset,
                                              size_t rows,
                                              size_t row_bytes,
                                              size_t stride_bytes) {
    if (!frame.buffer || !frame.buffer->Data()) {
        return nullptr;
    }
    
    // 空plane
    if (rows == 0 || row_bytes == 0) {
        return std::make_shared<CpuTensorBuffer>(0);
    }
    
    if (stride_bytes < row_bytes) {
        LOG_MAIN_ERROR_AT("MediaFrame plane stride is smaller than row bytes, row_bytes={}, stride={}",
                          row_bytes, stride_bytes);
        return nullptr;
    }

    const auto buffer_size = frame.buffer->Size();
    const auto required_end = offset + (rows - 1) * stride_bytes + row_bytes;
    if (offset >= buffer_size || required_end > buffer_size) {
        LOG_MAIN_ERROR_AT("MediaFrame plane is out of buffer range, offset={}, rows={}, row_bytes={}, stride={}, size={}",
                          offset,
                          rows,
                          row_bytes,
                          stride_bytes,
                          buffer_size);
        return nullptr;
    }

    // 行跨度等于行字节数，创建对应的MediaFramePlaneTensorBuffer
    if (stride_bytes == row_bytes) {
        return std::make_shared<MediaFramePlaneTensorBuffer>(frame.buffer, offset, rows * row_bytes);
    }
    
    // 行跨度不等于行字节数，分配一块紧凑的新内存，大小 = rows × row_bytes（不含 padding）
    auto compact = std::make_shared<CpuTensorBuffer>(rows * row_bytes);
    const auto* src = frame.buffer->Data() + offset;
    auto* dst = static_cast<uint8_t*>(compact->Data());
    for (size_t row = 0; row < rows; ++row) {
        // 只拷贝每一行有效的数据
        std::memcpy(dst + row * row_bytes, src + row * stride_bytes, row_bytes);
    }
    return compact;
}

/// @brief 创建一个 TensorPlane 实例，用于存储图像平面数据
std::unique_ptr<TensorPlane> MakeTensor(const std::string& name,
                                       TensorShape shape,
                                       std::shared_ptr<TensorBuffer> buffer) {
    if (!buffer) {
        return nullptr;
    }

    auto tensor = std::make_unique<TensorPlane>();
    tensor->name = name;
    tensor->type = TensorType::UINT8;
    tensor->shape = std::move(shape);
    tensor->SetBuffer(std::move(buffer), TensorMemoryType::OPENVINO_CPU);
    return tensor;
}

bool ValidateFrame(const MediaFrame& frame) {
    if (!frame.buffer || !frame.buffer->Data() || frame.buffer->Size() == 0) {
        LOG_MAIN_ERROR_AT("MediaFrame buffer is empty");
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0) {
        LOG_MAIN_ERROR_AT("MediaFrame size is invalid: {}x{}", frame.width, frame.height);
        return false;
    }
    if ((frame.width % 2) != 0 || (frame.height % 2) != 0) {
        LOG_MAIN_ERROR_AT("YUV420 frame size must be even: {}x{}", frame.width, frame.height);
        return false;
    }
    return true;
}

TensorMeta BuildTensorMeta(const MediaFrame& frame, uint32_t input_width, uint32_t input_height) {
    TensorMeta meta;
    meta.src_width = frame.width;
    meta.src_height = frame.height;
    meta.input_width = static_cast<int>(input_width);
    meta.input_height = static_cast<int>(input_height);

    if (frame.width > 0 && frame.height > 0) {
        meta.letterbox.scale_x = static_cast<float>(input_width) / static_cast<float>(frame.width);
        meta.letterbox.scale_y = static_cast<float>(input_height) / static_cast<float>(frame.height);
    }
    meta.letterbox.pad_x = 0.0f;
    meta.letterbox.pad_y = 0.0f;
    return meta;
}

TensorFrame PackNv12(const MediaFrame& frame) {
    TensorFrame tensor_frame;

    const auto width = static_cast<size_t>(frame.width);
    const auto height = static_cast<size_t>(frame.height);
    const auto y_stride = static_cast<size_t>(PlaneStride(frame, 0, frame.width));
    const auto uv_stride = static_cast<size_t>(PlaneStride(frame, 1, frame.width));
    const auto y_offset = PlaneOffset(frame, 0, 0);
    const auto uv_offset = PlaneOffset(frame, 1, y_stride * height);

    auto y = MakeTensor("y",
                        TensorShape{{1, frame.height, frame.width, 1}},
                        MakePlaneBuffer(frame, y_offset, height, width, y_stride));
    auto uv = MakeTensor("uv",
                         TensorShape{{1, frame.height / 2, frame.width / 2, 2}},
                         MakePlaneBuffer(frame, uv_offset, height / 2, width, uv_stride));

    if (y) {
        tensor_frame.AddTensor(std::move(y));
    }
    if (uv) {
        tensor_frame.AddTensor(std::move(uv));
    }
    return tensor_frame;
}

TensorFrame PackI420(const MediaFrame& frame) {
    TensorFrame tensor_frame;

    const auto width = static_cast<size_t>(frame.width);
    const auto height = static_cast<size_t>(frame.height);
    const auto half_width = width / 2;
    const auto half_height = height / 2;
    const auto y_stride = static_cast<size_t>(PlaneStride(frame, 0, frame.width));
    const auto u_stride = static_cast<size_t>(PlaneStride(frame, 1, frame.width / 2));
    const auto v_stride = static_cast<size_t>(PlaneStride(frame, 2, frame.width / 2));
    const auto y_offset = PlaneOffset(frame, 0, 0);
    const auto u_offset = PlaneOffset(frame, 1, y_stride * height);
    const auto v_offset = PlaneOffset(frame, 2, u_offset + u_stride * half_height);

    auto y = MakeTensor("y", TensorShape{{1, frame.height, frame.width, 1}},
                        MakePlaneBuffer(frame, y_offset, height, width, y_stride));
    auto u = MakeTensor("u", TensorShape{{1, frame.height / 2, frame.width / 2, 1}},
                        MakePlaneBuffer(frame, u_offset, half_height, half_width, u_stride));
    auto v = MakeTensor("v", TensorShape{{1, frame.height / 2, frame.width / 2, 1}},
                        MakePlaneBuffer(frame, v_offset, half_height, half_width, v_stride));

    if (y) {
        tensor_frame.AddTensor(std::move(y));
    }
    if (u) {
        tensor_frame.AddTensor(std::move(u));
    }
    if (v) {
        tensor_frame.AddTensor(std::move(v));
    }
    return tensor_frame;
}

}  // namespace

bool OpenVinoYoloPreprocessor::Initialize(const OpenVinoYoloPreprocessorConfig& config) {
    if (config.input_width == 0 || config.input_height == 0) {
        LOG_MAIN_ERROR_AT("OpenVINO preprocessor input size is invalid: {}x{}", config.input_width, config.input_height);
        return false;
    }

    input_width_ = config.input_width;
    input_height_ = config.input_height;
    pixel_format_ = config.pixel_format;
    return true;
}

TensorFrame OpenVinoYoloPreprocessor::Process(const MediaFrame& frame) {
    if (!ValidateFrame(frame)) {
        return {};
    }

    if (pixel_format_ != PixelFormat::kUnknown && frame.pixel_format != pixel_format_) {
        LOG_MAIN_WARN_AT("MediaFrame pixel format does not match preprocessor config");
    }

    TensorFrame tensor_frame;
    switch (frame.pixel_format) {
        case PixelFormat::kNV12:
            tensor_frame = PackNv12(frame);
            break;
        case PixelFormat::kI420:
            tensor_frame = PackI420(frame);
            break;
        case PixelFormat::kNV21:
            LOG_MAIN_ERROR_AT("NV21 is not supported by OpenVINO color preprocess directly; use NV12 or I420");
            return {};
        default:
            LOG_MAIN_ERROR_AT("Unsupported OpenVINO YUV preprocess pixel format");
            return {};
    }

    tensor_frame.tensor_meta_ = BuildTensorMeta(frame, input_width_, input_height_);
    return tensor_frame;
}