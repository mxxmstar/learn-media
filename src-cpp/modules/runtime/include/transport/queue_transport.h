#pragma once

/**
 * @file queue_transport.h
 * @brief 基于队列的异步传输通道
 *
 * 默认的 Transport 实现。核心是 IMailBox<T> 接口的数据通道：
 * - 生产侧通过 Send 入队（经过背压策略）
 * - 消费侧通过 Drain 循环出队（由 EdgeContext::ExecuteDrain 驱动）
 *
 * 异步通知机制：
 *   SetNotifyCallback 设置通知函数（即 Scheduler::Notify），
 *   每次成功入队后调用，触发下游 Drain 循环调度。
 *
 * 统计回调：
 *   SetSendResultCallback 设置在 Graph 层收集 metrics：
 *     - Accepted → enqueued 计数
 *     - DroppedOldest → enqueued + dropped 各 +1
 *     - DroppedNewest / Closed → rejected +1
 *
 * @tparam T 数据类型
 */

#include "i_transport.h"
#include "transport/i_mailbox.h"
#include "transport/mailbox.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

namespace runtime {

template<typename T>
class QueueTransport : public ITransport<T> {
public:
    /// 通知回调类型（传递给 Scheduler::Notify，触发下游 Drain）
    using NotifyCallback = std::function<void()>;

    /// 发送结果回调类型（用于 metrics 统计）
    using SendResultCallback = std::function<void(MailboxPushResult)>;

    /**
     * @brief 构造函数
     * @param mailbox 共享的 Mailbox 实例
     *
     * 同一个 mailBox_ 被 Send（生产者）和 TryReceive（消费者，Drain 端）
     * 共享。Graph::Connect 负责构造 QueueTransport 并传入上下游各自的 mailbox。
     */
    explicit QueueTransport(std::shared_ptr<IMailBox<T>> mailbox)
        : mailBox_(std::move(mailbox)),
          backpressurePolicy_(BackpressurePolicy::DropOldest) {}

    QueueTransport(std::size_t capacity, BackpressurePolicy policy)
        // : mailBox_(std::make_shared<MPMCMailBox<T>>(capacity)),
         : mailBox_(CreateMailBox<T>(MailBoxKind::SPSC, capacity)),
          backpressurePolicy_(policy) {}

    /**
     * @brief 设置背压策略
     *
     * 通常情况下 Graph::Connect 会根据 TransportOptions 设置。
     * 媒体管线推荐 DropOldest（丢弃旧帧保延迟）。
     */
    void SetBackpressurePolicy(BackpressurePolicy policy) {
        backpressurePolicy_ = policy;
    }

    /**
     * @brief 设置通知回调
     *
     * 当有数据入队后，QueueTransport 会调用这个回调通知调度器。
     * 调度器根据 callback 把 Drain 任务提交到 Executor。
     *
     * 回调内容是 Scheduler::Notify(edge_context_weak_ptr) 的包装。
     */
    void SetNotifyCallback(NotifyCallback callback) {
        notifyCallback_ = std::move(callback);
    }

    /**
     * @brief 设置发送结果回调（metrics 统计用）
     *
     * 由 Graph::Connect 在创建 Transport 时注册。
     * 回调内容是对应 NodeMetrics 的 enqueued / dropped / rejected 递增。
     */
    void SetSendResultCallback(SendResultCallback callback) {
        sendResultCallback_ = std::move(callback);
    }

    /**
     * @brief 发送数据（入队）
     *
     * 完整流程：
     *   1. 调用 mailBox_->Push(data, policy) 入队
     *   2. 统计 SendResultCallback
     *   3. 如果 Accepted / DroppedOldest（数据已入队），调用 NotifyCallback
     *      通知调度器驱动的 Drain
     *
     * 为什么 DroppedOldest 也触发通知？
     *   即使旧数据被丢弃，新数据确实入队了。下游收到通知后，
     *   ExecuteDrain 会消费这个新数据。
     */
    MailboxPushResult Send(T data) override {
        MailboxPushResult result = mailBox_->Push(std::move(data), backpressurePolicy_);

        if (sendResultCallback_) {
            sendResultCallback_(result);
        }

        // 数据被实际处理（入队或淘汰入队）时才触发通知
        if (result == MailboxPushResult::Accepted ||
            result == MailboxPushResult::DroppedOldest) {
            if (notifyCallback_) {
                notifyCallback_();
            }
        }

        return result;
    }

    /**
     * @brief 非阻塞接收
     *
     * 返回 std::optional<T>，避免要求 T 有默认构造。
     * ExecuteDrain 循环调用此方法直到返回 nullopt。
     *
     * 注意：mailBox_->TryPop(T&) 改用内部的 TryPopValue：
     *   - SPSC: TryPop 返回 bool + out-param
     *   - 包装成 std::optional<T> 对外暴露
     */
    std::optional<T> TryReceive() override {
        return TryPopValue();
    }

    /// @brief 阻塞接收（包装自旋等待的 WaitPop）
    std::optional<T> Receive() override {
        return WaitPopValue();
    }

    void Close() override {
        mailBox_->Close();
    }

    bool Empty() const override {
        return mailBox_->Empty();
    }

    std::size_t Size() const override {
        return mailBox_->Size();
    }

private:
    /**
     * @brief 从 mailbox 尝试非阻塞取值
     *
     * 绕过 IMailBox::TryPop(T&) 的 out-param 设计，
     * 返回 std::optional<T> 以避免 T 必须默认构造的限制。
     *
     * SPSC 场景：TryPop + 返回值包装
     * MPMC 场景：同
     *
     * 如果 mailbox 已关闭且队列空，返回 nullopt。
     */
    std::optional<T> TryPopValue() {
        T item;
        if (mailBox_->TryPop(item)) {
            return std::move(item);
        }
        return std::nullopt;
    }

    /// @brief 从 mailbox 阻塞取值
    std::optional<T> WaitPopValue() {
        T item;
        if (mailBox_->WaitPop(item)) {
            return std::move(item);
        }
        return std::nullopt;
    }

    std::shared_ptr<IMailBox<T>> mailBox_;   ///< 底层数据通道
    BackpressurePolicy backpressurePolicy_;  ///< 背压策略
    NotifyCallback notifyCallback_;          ///< 通知回调（唤醒下游 Drain）
    SendResultCallback sendResultCallback_;  ///< 统计回调
};

}
