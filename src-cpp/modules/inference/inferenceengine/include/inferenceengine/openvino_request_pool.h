#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include <openvino/openvino.hpp>

/// @brief OpenVINO 推理请求池
class OpenVinoInferRequestPool {
public:
    /// @brief 初始化推理池
    /// @param model 编译后的模型
    /// @param request_count 推理请求数量
    /// @return 是否初始化成功
    bool Initialize(ov::CompiledModel& model, uint32_t request_count);
    /// @brief 获取推理请求
    std::shared_ptr<ov::InferRequest> Acquire();
    /// @brief 释放推理请求
    void Release(std::shared_ptr<ov::InferRequest> request);
    /// @brief 获取空闲推理请求数量
    size_t IdleCount() const;
    /// @brief 获取总推理请求数量
    size_t TotalCount() const;

private:
    /// @brief 空闲推理请求队列
    std::queue<std::shared_ptr<ov::InferRequest>> idle_requests_;
    /// @brief 互斥锁
    mutable std::mutex mutex_;
    /// @brief 条件变量
    std::condition_variable cv_;
    /// @brief 总推理请求数量
    size_t total_count_{0};
};
