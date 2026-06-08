#pragma once

#include <cstddef>

namespace runtime {

/**
 * @brief 背压策略枚举
 *
 * 当 Mailbox 满时，Push 操作根据此策略决定行为：
 *
 * - Block:      生产者阻塞等待，直到队列有空位或队列被关闭。
 *               适用于下游必须处理每一帧、不允许丢弃的场景（如录制）。
 *
 * - DropNewest: 丢弃新到达的数据，保留队列中的旧数据。
 *               适用于最新帧不重要、旧帧优先处理的场景（如逐帧分析）。
 *
 * - DropOldest: 丢弃队列中最旧的数据，让新数据入队。
 *               默认策略。适用于实时视频场景——丢弃旧帧保证延迟最低，
 *               播放端总是看到最新帧。
 *
 * - Unbounded:  队列无限制增长（内存无上限）。
 *               用于生产者远快于消费者但绝不能丢帧的调试场景，
 *               生产环境慎用，可能导致 OOM。
 */
enum class BackpressurePolicy {
    Block,
    DropNewest,
    DropOldest,
    Unbounded
};

/**
 * @brief Push 操作结果枚举
 *
 * Accepted:      数据成功入队，消费端最终会处理。
 * DroppedNewest: 队列满且策略为 DropNewest，新数据被丢弃——不做入队。
 * DroppedOldest: 队列满且策略为 DropOldest，旧数据被弹出淘汰，新数据入队。
 *                此时 enqueued 和 dropped 各 +1。
 * Closed:        队列已关闭，拒绝入队。通常发生在 Graph::Stop() 之后。
 */
enum class MailboxPushResult {
    Accepted,
    DroppedNewest,
    DroppedOldest,
    Closed
};

/**
 * @brief Mailbox 类型枚举
 *
 * SPSC: 单生产者-单消费者，内部使用 boost::lockfree::spsc_queue，
 *       完全无锁，性能最高。适用于标准的管线场景（一个上游一个下游）。
 *
 * MPMC: 多生产者-多消费者，内部使用 moodycamel::ConcurrentQueue，
 *       多生产者路径也是无锁的。适用多个上游汇入同一节点的场景。
 */
enum class MailBoxKind {
    SPSC,
    MPMC
};

/**
 * @brief Mailbox 抽象接口
 *
 * 线程间异步通信通道，核心原语是 Push（生产者侧）和 Pop（消费者侧）。
 *
 * 与 std::queue / std::deque 的区别：
 *   - 线程安全，支持跨线程 Push/Pop
 *   - 支持背压策略（满时阻塞/丢弃）
 *   - 支持生命周期管理（Open/Close）
 *   - SPSC 实现完全无锁，无系统调用
 *
 * @tparam T 元素类型，要求可移动构造
 */
template <typename T>
class IMailBox {
public:
    virtual ~IMailBox() = default;

    /// @brief 入队。返回操作结果（Accepted / DroppedNewest / DroppedOldest / Closed）
    virtual MailboxPushResult Push(T item, BackpressurePolicy policy) = 0;

    /// @brief 非阻塞出队。true 成功，false 队列空
    virtual bool TryPop(T& item) = 0;

    /// @brief 阻塞出队。true 成功，false 队列已关闭（等待期间其他线程调用 Close）
    virtual bool WaitPop(T& item) = 0;

    /// @brief 关闭队列，唤醒所有阻塞在 WaitPop 的线程，之后 Push 返回 Closed
    virtual void Close() = 0;

    /// @brief 重新打开队列，允许 Push
    virtual void Open() = 0;

    /// @brief 清空队列中所有数据
    virtual void Clear() = 0;

    /// @brief 队列是否为空
    virtual bool Empty() const = 0;

    /// @brief 当前元素数量
    virtual std::size_t Size() const = 0;

    /// @brief 队列容量（0 表示无界）
    virtual std::size_t Capacity() const = 0;

    /// @brief 队列是否已关闭
    virtual bool IsClosed() const = 0;
};

}
