#pragma once

/**
 * @file mpmc_mailbox.h
 * @brief 多生产者-多消费者 Mailbox 实现
 *
 * 基于 moodycamel::ConcurrentQueue（无锁 MPMC 队列）。
 * 适用场景：多个上游节点同时 Push（例如多路 RTSP 源汇入同一 AI 检测节点）。
 *
 * 与 SPSCMailBox 的区别：
 *   - 支持多生产者并发入队，内部无锁
 *   - 性能略低于 SPSC（多生产者路径有 CAS 竞争）
 *   - Unbounded 模式下仅限 MPMC 才有意义（SPSC 的 ring buffer 无法动态扩容）
 *
 * @tparam T 元素类型，要求可移动构造
 */

#include "transport/i_mailbox.h"
#include "common/queue/mpmc_queue.h"

#include <atomic>
#include <thread>

namespace runtime {

template <typename T>
class MPMCMailBox : public IMailBox<T> {
public:
    /// @brief 构造函数
    /// @param capacity 容量（0 表示无界）。注意 MPMC 的容量是近似值
    explicit MPMCMailBox(std::size_t capacity = 64)
        : queue_(capacity), capacity_(capacity) {}

    MPMCMailBox(const MPMCMailBox&) = delete;
    MPMCMailBox& operator=(const MPMCMailBox&) = delete;

    /**
     * @brief 入队（多生产者安全）
     *
     * 分支逻辑：
     *   Unbounded / capacity==0 → 自旋直到入队成功（mq 会内部扩容）
     *   Block → 自旋等待
     *   正常路径 → try_enqueue，失败则按策略处理
     *   DropOldest → pop 一个旧元素腾空间再 push
     *
     * 注意：moodycamel 的 enqueue 不会因"队列满"而返回 false，
     * 它会动态分配新节点。所以实际用来限制容量的方式是"如果 enqueue
     * 失败就认为是满了"——在高并发下这只是近似控制。
     */
    MailboxPushResult Push(T item, BackpressurePolicy policy) override {
        if (closed_.load(std::memory_order_acquire)) {
            return MailboxPushResult::Closed;
        }

        // Unbounded 或无界：自旋直到入队成功
        if (policy == BackpressurePolicy::Unbounded || capacity_ == 0) {
            while (!queue_.push(std::move(item))) {
                if (closed_.load(std::memory_order_acquire)) {
                    return MailboxPushResult::Closed;
                }
                std::this_thread::yield();
            }
            return MailboxPushResult::Accepted;
        }

        // Block：自旋等待入队
        if (policy == BackpressurePolicy::Block) {
            while (true) {
                if (queue_.push(std::move(item))) {
                    return MailboxPushResult::Accepted;
                }
                if (closed_.load(std::memory_order_acquire)) {
                    return MailboxPushResult::Closed;
                }
                std::this_thread::yield();
            }
        }

        // 快速路径：enqueue 成功
        if (queue_.push(std::move(item))) {
            return MailboxPushResult::Accepted;
        }

        // DropOldest：pop 一个旧元素尝试腾出空间
        if (policy == BackpressurePolicy::DropOldest) {
            T dropped_item;
            if (queue_.pop(dropped_item)) {
                if (queue_.push(std::move(item))) {
                    return MailboxPushResult::DroppedOldest;
                }
            }
        }

        return MailboxPushResult::DroppedNewest;
    }

    /// @brief 非阻塞出队
    bool TryPop(T& item) override {
        return queue_.pop(item);
    }

    /// @brief 阻塞出队（自旋等待）
    bool WaitPop(T& item) override {
        while (true) {
            if (queue_.pop(item)) {
                return true;
            }
            if (closed_.load(std::memory_order_acquire)) {
                return false;
            }
            std::this_thread::yield();
        }
    }

    void Close() override {
        closed_.store(true, std::memory_order_release);
    }

    void Open() override {
        closed_.store(false, std::memory_order_release);
    }

    /// @brief 清空队列（通过循环 pop 实现，不依赖 T 的默认构造）
    void Clear() override {
        T item;
        while (queue_.pop(item)) {
        }
    }

    bool Empty() const override {
        return queue_.empty();
    }

    /// @brief 返回近似大小（moodycamel 的 size_approx）
    std::size_t Size() const override {
        return queue_.size();
    }

    std::size_t Capacity() const override {
        return capacity_;
    }

    bool IsClosed() const override {
        return closed_.load(std::memory_order_acquire);
    }

private:
    common::BoundedMpmcQueue<T> queue_;   ///< moodycamel::ConcurrentQueue 包装
    std::size_t capacity_;
    std::atomic<bool> closed_{false};
};

}
