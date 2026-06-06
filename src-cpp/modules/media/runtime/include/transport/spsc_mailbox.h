#pragma once

/// @file spsc_mailbox.h
/// @brief 单生产者-单消费者 Mailbox 实现
///
/// 默认的 Mailbox 实现，性能最优：
/// - 使用 BoundedSpscQueue（无锁环形缓冲区）
/// - 完全无锁实现，使用自旋等待替代阻塞
/// - 针对视频编解码流水线优化

#include "transport/i_mailbox.h"
#include "common/queue/spsc_queue.h"

#include <atomic>
#include <thread>

namespace runtime {

/// @brief 单生产者-单消费者 Mailbox 实现
/// @tparam T 元素类型
template <typename T>
class SPSCMailBox : public IMailBox<T> {
public:
    /// @brief 构造函数
    /// @param capacity 容量
    explicit SPSCMailBox(std::size_t capacity = 64)
        : queue_(capacity), capacity_(capacity) {}

    SPSCMailBox(const SPSCMailBox&) = delete;
    SPSCMailBox& operator=(const SPSCMailBox&) = delete;

    /// @brief 推入元素（无锁）
    /// @param item 元素
    /// @param policy 背压策略
    /// @return 操作结果
    MailboxPushResult Push(T item, BackpressurePolicy policy) override {
        if (closed_.load(std::memory_order_acquire)) {
            return MailboxPushResult::Closed;
        }

        if (policy == BackpressurePolicy::Block) {
            while (true) {
                if (!queue_.full() && queue_.push(std::move(item))) {
                    return MailboxPushResult::Accepted;
                }
                if (closed_.load(std::memory_order_acquire)) {
                    return MailboxPushResult::Closed;
                }
                std::this_thread::yield();
            }
        }

        if (!queue_.full() && queue_.push(std::move(item))) {
            return MailboxPushResult::Accepted;
        }

        if (policy == BackpressurePolicy::DropOldest) {
            T dropped{};
            if (queue_.pop(dropped)) {
                if (queue_.push(std::move(item))) {
                    return MailboxPushResult::DroppedOldest;
                }
            }
        }

        return MailboxPushResult::DroppedNewest;
    }

    /// @brief 非阻塞弹出
    bool TryPop(T& item) override {
        return queue_.pop(item);
    }

    /// @brief 阻塞等待弹出
    bool WaitPop(T& item) override {
        while (true) {
            if (queue_.pop(item)) {
                return true;
            }
            if (closed_.load(std::memory_order_acquire)) {
                return false;
            }
            std::this_thread::yield();  // 自旋等待
        }
    }

    void Close() override {
        closed_.store(true, std::memory_order_release);
    }

    void Open() override {
        closed_.store(false, std::memory_order_release);
    }

    void Clear() override {
        queue_.clear();
    }

    bool Empty() const override {
        return queue_.empty();
    }

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
    common::BoundedSpscQueue<T> queue_;
    std::size_t capacity_;
    std::atomic<bool> closed_{false};
};

} // namespace runtime