#pragma once

/// @file mpmc_mailbox.h
/// @brief 多生产者-多消费者 Mailbox 实现
///
/// 兼容原 Mailbox<T> 的语义，使用 mpmc_queue 实现。
/// 使用无锁队列实现，高性能的多生产者-多消费者场景。
#include "transport/i_mailbox.h"
#include "common/queue/mpmc_queue.h"
#include <atomic>
#include <thread>
namespace runtime {

/// @brief 多生产者-多消费者 Mailbox 实现
/// @tparam T 元素类型
template <typename T>
class MPMCMailBox : public IMailBox<T> {
public:
    /// @brief 构造函数
    /// @param capacity 容量（0 表示无界）
    explicit MPMCMailBox(std::size_t capacity = 64)
        : queue_(capacity), capacity_(capacity) {}

    MPMCMailBox(const MPMCMailBox&) = delete;
    MPMCMailBox& operator=(const MPMCMailBox&) = delete;

    /// @brief 推入元素（无锁），内部会自旋等待直到成功入队
    MailboxPushResult Push(T item, BackpressurePolicy policy) override {
        if (closed_.load(std::memory_order_acquire)) {
            return MailboxPushResult::Closed;
        }

        if (policy == BackpressurePolicy::Unbounded || capacity_ == 0) {
            while (!queue_.push(std::move(item))) {
                if (closed_.load(std::memory_order_acquire)) {
                    return MailboxPushResult::Closed;
                }
                std::this_thread::yield();  // 自旋等待
            }
            return MailboxPushResult::Accepted;
        }

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

        if (queue_.push(std::move(item))) {
            return MailboxPushResult::Accepted;
        }

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

    bool TryPop(T& item) override {
        return queue_.pop(item);
    }

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

    void Clear() override {
        T item;
        while (queue_.pop(item)) {
        }
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
    common::BoundedMpmcQueue<T> queue_;
    std::size_t capacity_;
    std::atomic<bool> closed_{false};
};

} // runtime