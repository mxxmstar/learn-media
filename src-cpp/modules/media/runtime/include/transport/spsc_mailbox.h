#pragma once

/**
 * @file spsc_mailbox.h
 * @brief 单生产者-单消费者 Mailbox 实现
 *
 * 管线默认使用的 Mailbox，基于 boost::lockfree::spsc_queue（无锁环形缓冲区）。
 * 核心特性：
 *   - Push/TryPop 完全无锁（仅 atomic 操作，无系统调用）
 *   - WaitPop 自旋等待（yield），适合低延迟场景
 *   - Block 策略也使用自旋而非条件变量——避免线程睡眠/唤醒开销
 *
 * 与通用 SPSC 队列的区别：
 *   - 支持背压策略（DropOldest / DropNewest / Block）
 *   - 支持 Open/Close 生命周期
 *   - 只允许一个生产者线程和一个消费者线程
 *
 * @tparam T 元素类型，要求可移动构造，不需默认构造
 */

#include "transport/i_mailbox.h"
#include "common/queue/spsc_queue.h"

#include <atomic>
#include <thread>

namespace runtime {

template <typename T>
class SPSCMailBox : public IMailBox<T> {
public:
    /// @brief 构造函数
    /// @param capacity 环形缓冲区容量（槽位数），管线上游太快时触发背压
    explicit SPSCMailBox(std::size_t capacity = 64)
        : queue_(capacity), capacity_(capacity) {}

    SPSCMailBox(const SPSCMailBox&) = delete;
    SPSCMailBox& operator=(const SPSCMailBox&) = delete;

    /**
     * @brief 无锁入队
     *
     * 判断路径（按频率排列）：
     *   1. [最快] 队列未满 → 直接 push，返回 Accepted
     *   2. DropOldest → 尝试 pop 一个旧元素腾出空间，再 push
     *   3. Block → 自旋等空位
     *   4. [最慢] 队列已满且不可丢弃 → 返回 DroppedNewest
     *
     * Q: 为什么不优先用 Block + 条件变量？
     * A: 媒体管线是数据流式处理，WaitPop 端已经在自旋等数据；
     *    引入 cv 会在高吞吐下增加不必要的线程切换和唤醒延迟。
     *    自旋在队列不满时通常只持续几十 ns。
     */
    MailboxPushResult Push(T item, BackpressurePolicy policy) override {
        if (closed_.load(std::memory_order_acquire)) {
            return MailboxPushResult::Closed;
        }

        // Block 策略：自旋等待直到有空位或队列关闭
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

        // DropNewest / DropOldest / Unbounded 的快速路径：队列不满直接入队
        if (!queue_.full() && queue_.push(std::move(item))) {
            return MailboxPushResult::Accepted;
        }

        // DropOldest：弹出最旧元素腾出空间，再入队
        // 注意：MediaFrame 等大对象可能没有默认构造，这里用移动避免拷贝
        if (policy == BackpressurePolicy::DropOldest) {
            T dropped{};
            if (queue_.pop(dropped)) {
                if (queue_.push(std::move(item))) {
                    return MailboxPushResult::DroppedOldest;
                }
            }
        }

        // 队列满且策略不允许丢弃 → 丢弃新数据
        return MailboxPushResult::DroppedNewest;
    }

    /// @brief 非阻塞出队。true 成功，false 队列空
    bool TryPop(T& item) override {
        return queue_.pop(item);
    }

    /**
     * @brief 阻塞出队
     *
     * 使用自旋（yield）而非条件变量。原因：
     *   QueueTransport::Receive 在 EdgeContext::ExecuteDrain 中被调用，
     *   ExecuteDrain 使用 TryReceive 非阻塞版本，所以 WaitPop 只在
     *   外部直接调用时才会用到——这种情况较少，自旋可接受。
     */
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

    /// @brief 关闭队列。Close 后被 Push 返回 Closed，WaitPop 返回 false
    void Close() override {
        closed_.store(true, std::memory_order_release);
    }

    /// @brief 重新打开队列
    void Open() override {
        closed_.store(false, std::memory_order_release);
    }

    /// @brief 清空队列。调用底层 BoundedSpscQueue::clear，不依赖 T 的默认构造
    void Clear() override {
        queue_.clear();
    }

    bool Empty() const override {
        return queue_.empty();
    }

    std::size_t Size() const override {
        return queue_.size();
    }

    /// @brief 返回构造时传入的容量。注意：实际可用槽位数可能因实现而略少
    std::size_t Capacity() const override {
        return capacity_;
    }

    bool IsClosed() const override {
        return closed_.load(std::memory_order_acquire);
    }

private:
    common::BoundedSpscQueue<T> queue_;   ///< boost::lockfree::spsc_queue 包装
    std::size_t capacity_;                ///< 构造容量
    std::atomic<bool> closed_{false};     ///< 关闭状态（内存序：release/acquire）
};

}
