#pragma once

/**
 * @file output_port.h
 * @brief 输出端口——节点发送数据的接口
 *
 * OutputPort 是数据离开节点的唯一出口。一个 OutputPort 可以连接
 * 一个或多个下游 Transport（多播，fan-out）。
 *
 * 性能关键路径：
 *   Send 方法被节点 Process 调用，需尽可能轻量。
 *   实现上区分"单下游"和"多下游"两条路径：
 *
 *   单下游（transports_.size() == 1）：
 *     使用 std::move 移动语义传输，零拷贝。
 *
 *   多下游（transports_.size() > 1）：
 *     需要拷贝 data 给每个下游（由 is_copy_constructible_v<T> 约束）。
 *     若 T 不可拷贝，编译期报错——这是正确的（不能把同一帧给多个消费端
 *     却不拷贝）。
 *
 * Send 返回 bool 而不是 MailboxPushResult：
 *   因为多下游时每个 transport 的返回值可能不同，IsAccepted 聚合逻辑：
 *   - Accepted / DroppedOldest → true（数据被消费了）
 *   - DroppedNewest / Closed → false（数据被丢弃或通道关闭）
 *
 * @tparam T 数据类型
 */

#include "transport/i_transport.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace runtime {

template<typename T>
class ITransport;

template<typename T>
class OutputPort {
public:
    using Type = T;

    /// @brief 添加下游传输通道。Graph::Connect 调用
    void AddTransport(std::shared_ptr<ITransport<T>> transport) {
        transports_.push_back(std::move(transport));
    }

    /**
     * @brief 发送数据到所有下游
     * @param data 待发送数据
     * @return true=至少一个下游接受了数据，false=所有下游都丢弃/关闭
     *
     * 单下游（最常见）：移动语义
     * 多下游：拷贝语义（需要 T 可拷贝构造）
     */
    bool Send(T data) {
        if (transports_.empty()) {
            return true;
        }

        // —— 单下游：最频繁的路径，使用移动语义 ——
        if (transports_.size() == 1) {
            return IsAccepted(transports_.front()->Send(std::move(data)));
        }

        // —— 多下游（fan-out）：拷贝到每个下游 ——
        if constexpr (std::is_copy_constructible_v<T>) {
            bool accepted = true;
            for (auto& transport : transports_) {
                // 注意：这里用拷贝而非移动，因为要发给多个下游
                accepted = IsAccepted(transport->Send(data)) && accepted;
            }
            return accepted;
        } else {
            // T 不可拷贝却连接了多个下游——编译期就应避免这种配置
            return false;
        }
    }

private:
    /// @brief 判断一个 Send 结果是否算"成功接受"
    /// Accepted: 正常入队；DroppedOldest: 旧帧被替换，也算接受
    static bool IsAccepted(MailboxPushResult result) {
        return result == MailboxPushResult::Accepted ||
               result == MailboxPushResult::DroppedOldest;
    }

    std::vector<std::shared_ptr<ITransport<T>>> transports_;
};

}
