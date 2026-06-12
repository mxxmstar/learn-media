#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "defines/media_frame.hpp"
#include "inferenceinfo/result.h"
#include "tensordata/tensor_frame.h"

/// @brief 模型配置结构体，仅描述模型通用配置
struct ModelConfig {
    /// @brief 模型名称
    std::string name{""};
    /// @brief 模型路径
    std::string path{""};
    /// @brief 推理后端
    std::string backend{"OPENVINO"};
    /// @brief 计算设备
    std::string device{"CPU"};

    /// @brief 批量大小，默认值为 1
    int batch_size{1};
    /// @brief 推理请求池大小，默认值为 1
    uint32_t request_count{1};

    /// @brief 是否支持动态形状
    bool dynamic_shape{false};
};

/// @brief 模型语义层接口
class IModel {
public:
    virtual ~IModel() = default;

    /// @brief 初始化模型资源
    virtual bool Initialize(const ModelConfig& config) = 0;
    /// @brief 配置模型输入形状
    virtual bool ConfigureInputShape(const std::vector<std::size_t>& shape) {
        (void)shape;
        return true;
    }
    /// @brief 预处理媒体帧
    virtual TensorFrame Preprocess(const MediaFrame& frame) = 0;
    /// @brief 后处理模型输出张量
    virtual FrameResult Postprocess(const TensorFrame& output) = 0;
};
