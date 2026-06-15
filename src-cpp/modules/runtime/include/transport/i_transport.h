#pragma once

/**
 * @file i_transport.h
 * @brief 传输通道接口
 *
 * Transport 是"节点间的连接线"，定义数据如何从 OutputPort
 * 传输到 InputPort。ITransport 是纯虚接口，具体语义由子类实现：
 *
 *   QueueTransport    — 基于无锁 SPSC Mailbox，异步，可跨线程
 *   DirectTransport   — 同线程同步直传，调用方线程直接执行 consumer
 *
 * v2 接口变更说明：
 *   - Send 返回 MailboxPushResult 而非 bool，以便精确统计每种背压结果
 *   - TryReceive / Receive 返回 std::optional<T> 而非 bool+T&，
 *     完全消除 T 必须默认构造的限制
 *   - 新增 Close / Empty / Size 方法，支持 Graph::Stop 时关闭通道
 *
 * @tparam T 数据类型（MediaPacket / MediaFrame 等）
 */

#include "transport/i_mailbox.h"

#include <cstddef>
#include <optional>

namespace runtime {

template<typename T>
class ITransport {
public:
    virtual ~ITransport() = default;

    /**
     * @brief 发送数据到下游
     * @param data 待发送的数据（移动语义）
     * @return MailboxPushResult 精确区分 Accepted / DroppedNewest
     *         / DroppedOldest / Closed
     *
     * 对于 QueueTransport：实际写入 SPSCMailBox
     * 对于 DirectTransport：直接调用下游 consumer_
     */
    virtual MailboxPushResult Send(T data) = 0;

    /**
     * @brief 非阻塞接收
     * @return 有数据时返回 std::optional<T>，无数据时返回 std::nullopt
     *
     * EdgeContext::ExecuteDrain 使用 TryReceive 批量消费。
     * DirectTransport 不参与 Drain，始终返回 nullopt。
     */
    virtual std::optional<T> TryReceive() = 0;

    /**
     * @brief 阻塞接收
     * @return 有数据时返回 std::optional<T>，队列关闭时返回 nullopt
     *
     * QueueTransport 自旋等待直到有数据或队列关闭。
     * DirectTransport 始终返回 nullopt。
     */
    virtual std::optional<T> Receive() = 0;

    /// @brief 关闭传输通道。之后 Send 返回 Closed，Receive 可能返回 nullopt
    virtual void Close() = 0;

    /// @brief 通道中是否有未消费的数据
    virtual bool Empty() const = 0;

    /// @brief 通道中未消费的数据量（对于 DirectTransport 始终为 0）
    virtual std::size_t Size() const = 0;
};

}
