#pragma once

/**
 * @file direct_transport.h
 * @brief 同线程同步直传传输通道
 *
 * DirectTransport 不缓冲数据，Send() 直接调用下游 consumer_。
 * 调用方线程（上游 OutputPort::Send 所在的线程）同步执行下游逻辑。
 *
 * 适用场景：
 *   - 上下游必须在同一线程执行（避免上下文切换）
 *   - 想测试单个管线分段延迟时
 *   - 上游已经是异步的（已在 Executor 上），下游不需要额外线程
 *
 * 不适用场景：
 *   - 上下游需要不同优先级的线程（如 IO 线程不能阻塞做推理）
 *   - 上下游需要缓冲解耦防抖
 *
 * 注意：DirectTransport 不参与 Drain 循环。
 * TryReceive/Receive 始终返回 nullopt，Empty() 始终返回 true。
 */

#include "i_transport.h"

#include <cstddef>
#include <functional>
#include <optional>

namespace runtime {

template<typename T>
class DirectTransport : public ITransport<T> {
public:
    using Consumer = std::function<void(T)>;
    using SendResultCallback = std::function<void(MailboxPushResult)>;

    /// @brief 设置下游消费函数。由 Graph::Connect(Direct) 时自动设置
    void SetConsumer(Consumer consumer) {
        consumer_ = std::move(consumer);
    }

    /// @brief 设置发送结果回调，用于 Graph 统计 metrics
    void SetSendResultCallback(SendResultCallback cb) {
        on_send_result_ = std::move(cb);
    }

    /**
     * @brief 同步发送数据
     *
     * 流程：
     *   1. 检查 closed 状态
     *   2. 回调 on_send_result_ 统计 enqueued
     *   3. 直接调用 consumer_ 执行下游 Process
     *
     * consumer_ 内部可能会调用 OutputPort::Send 继续往下游传播——
     * 形成同线程上的同步调用链。
     */
    MailboxPushResult Send(T data) override {
        if (closed_) {
            if (on_send_result_) {
                on_send_result_(MailboxPushResult::Closed);
            }
            return MailboxPushResult::Closed;
        }

        if (on_send_result_) {
            on_send_result_(MailboxPushResult::Accepted);
        }

        // 同步调用下游，注意：可能递归进入下一级 DirectTransport
        if (consumer_) {
            consumer_(std::move(data));
        }

        return MailboxPushResult::Accepted;
    }

    /// @brief DirectTransport 不缓冲数据，始终返回 nullopt
    std::optional<T> TryReceive() override {
        return std::nullopt;
    }

    /// @brief DirectTransport 不参与阻塞读取，始终返回 nullopt
    std::optional<T> Receive() override {
        return std::nullopt;
    }

    /// @brief 关闭通道，此后 Send 返回 Closed
    void Close() override {
        closed_ = true;
    }

    /// @brief 始终返回 true（Drain 循环不会从 DirectTransport 取数据）
    bool Empty() const override {
        return true;
    }

    /// @brief 始终返回 0
    std::size_t Size() const override {
        return 0;
    }

private:
    Consumer consumer_;                  ///< 下游消费函数
    SendResultCallback on_send_result_;  ///< 统计回调
    bool closed_{false};                 ///< 关闭标记
};

}
