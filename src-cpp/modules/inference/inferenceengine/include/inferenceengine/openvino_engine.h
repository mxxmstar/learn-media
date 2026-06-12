#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <openvino/openvino.hpp>

#include "defines/media_frame.hpp"
#include "inferenceengine/i_engine.h"
#include "inferenceengine/openvino_request_pool.h"
#include "model/i_model.h"

/// @brief OpenVINO 预处理配置
struct OpenVinoPreprocessConfig {
    /// @brief 是否启用 OpenVINO 内部预处理
    bool enabled{false};
    /// @brief OpenVINO 预处理输入像素格式
    PixelFormat input_pixel_format{PixelFormat::kNV12};
    /// @brief 模型期望的像素格式
    PixelFormat model_pixel_format{PixelFormat::kBGR24};
    /// @brief 模型输入布局
    std::string model_input_layout{"NCHW"};
    /// @brief OpenVINO 缩放因子。0 表示不进行缩放
    float scale{0.0f};
};

/// @brief OpenVINO CPU 推理引擎
class OpenVinoCpuEngine : public IInferenceEngine {
public:
    OpenVinoCpuEngine();
    ~OpenVinoCpuEngine() override;

    /// @brief 加载模型
    bool LoadModel(const ModelConfig& config) override;
    /// @brief 设置 OpenVINO 预处理配置
    void SetPreprocessConfig(const OpenVinoPreprocessConfig& config);
    /// @brief 运行同步推理
    bool Infer(const TensorFrame& input, TensorFrame& output) override;
    /// @brief 运行异步推理
    bool InferAsync(const InferContext& ctx, const TensorFrame& input, InferCallback cb) override;
    /// @brief 释放推理引擎资源
    void Release() override;
    /// @brief 获取原始模型输入形状
    std::vector<std::size_t> GetInputShape() const override;

    /// @brief 等待所有异步推理请求完成
    bool WaitAll();

    /// @brief 获取输入张量描述
    std::vector<TensorDesc> GetInputDesc() const override;
    /// @brief 获取输出张量描述
    std::vector<TensorDesc> GetOutputDesc() const override;
    /// @brief 获取推理引擎信息
    EngineInfo GetEngineInfo() const override;
    /// @brief 获取内存类型
    TensorMemoryType GetMemoryType() const override;

private:
    /// @brief OpenVINO Core
    ov::Core core_;
    /// @brief 编译模型
    ov::CompiledModel compiled_model_;
    /// @brief 推理请求池
    std::unique_ptr<OpenVinoInferRequestPool> request_pool_;
    /// @brief 模型配置
    ModelConfig config_;
    /// @brief OpenVINO 预处理配置
    OpenVinoPreprocessConfig preprocess_config_;
    /// @brief 原始模型输入形状
    std::vector<std::size_t> model_input_shape_;
    /// @brief 初始化状态
    bool initialized_{false};
    /// @brief 是否接受新的异步推理请求
    std::atomic<bool> accepting_async_{false};
};
