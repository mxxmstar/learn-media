#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "model/i_model.h"
#include "preprocessor/i_preprocessor.h"

class YoloModel : public IModel {
public:
    /// @brief 初始化模型资源
    /// @param config 模型配置
    /// @return 是否初始化成功
    bool Initialize(const ModelConfig& config) override;

    /// @brief 配置模型输入形状
    /// @param shape 原始模型输入形状
    /// @return 是否配置成功
    bool ConfigureInputShape(const std::vector<std::size_t>& shape) override;

    /// @brief 预处理媒体帧
    /// 将原始 YUV plane 封装成 TensorFrame，让 OpenVINO 内部预处理处理颜色转换和 resize
    /// @param frame 媒体帧数据
    /// @return 处理后的张量包
    TensorFrame Preprocess(const MediaFrame& frame) override;

    /// @brief 后处理模型输出张量
    /// 解码器 + NMS + 结果
    /// @param output 模型输出张量
    /// @return 视频帧结果
    FrameResult Postprocess(const TensorFrame& output) override;

private:
    /// @brief 重置预处理器
    /// 重置预处理器的配置，用于处理不同的输入形状
    /// @return 是否重置成功
    bool ResetPreprocessor();

    std::unique_ptr<IPreprocessor> preprocessor_;   ///< 预处理器
    // TODO：放到postprocessor中
    float conf_threshold_ = 0.25f;  ///< 置信度阈值
    float nms_threshold_ = 0.45f;   ///< NMS 阈值
    int input_width_ = 640; ///< 模型输入宽度
    int input_height_ = 640; ///< 模型输入高度
    bool initialized_{false}; ///< 初始化状态
};
