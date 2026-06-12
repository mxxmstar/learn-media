#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "model/i_model.h"
#include "tensordata/tensor_frame.h"

/// @brief 推理引擎信息
struct EngineInfo {
    /// @brief 后端类型
    std::string backend;
    /// @brief 设备类型
    std::string device;
    /// @brief 是否支持异步推理
    bool support_async{false};
    /// @brief 是否支持动态形状
    bool support_dynamic_shape{false};
    /// @brief 最大批量大小
    uint32_t max_batch_size{1};
};

/// @brief 推理上下文   
struct InferContext {
    /// @brief 帧 ID
    uint64_t frame_id{0};
    /// @brief 时间戳
    uint64_t pts{0};
};

/// @brief 输入/输出张量描述
struct TensorDesc {
    /// @brief 张量名称
    std::string name;
    /// @brief 张量元素类型
    TensorType type{TensorType::UNKNOWN};
    /// @brief 张量形状
    TensorShape shape;
    /// @brief 是否动态形状
    bool dynamic_shape{false};
    /// @brief 是否输入张量
    bool is_input{true};
    /// @brief 张量元素数量
    size_t element_count{0};
    /// @brief 张量字节数
    size_t bytes{0};
};

using InferCallback = std::function<void(const InferContext&, TensorFrame&&)>;

/// @brief 推理引擎基类
/// 它仅加载模型、运行推理并返回输出张量
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;

    /// @brief 加载模型
    virtual bool LoadModel(const ModelConfig& config) = 0;
    /// @brief 运行同步推理
    virtual bool Infer(const TensorFrame& input, TensorFrame& output) = 0;
    /// @brief 运行异步推理
    virtual bool InferAsync(const InferContext& ctx, const TensorFrame& input, InferCallback cb) = 0;
    /// @brief 释放推理引擎资源
    virtual void Release() = 0;

    /// @brief 获取原始模型输入形状
    virtual std::vector<std::size_t> GetInputShape() const = 0;

    /// @brief 获取输入张量描述
    virtual std::vector<TensorDesc> GetInputDesc() const = 0;
    /// @brief 获取输出张量描述
    virtual std::vector<TensorDesc> GetOutputDesc() const = 0;
    /// @brief 获取推理引擎信息
    virtual EngineInfo GetEngineInfo() const = 0;
    /// @brief 获取内存类型
    virtual TensorMemoryType GetMemoryType() const = 0;
};
