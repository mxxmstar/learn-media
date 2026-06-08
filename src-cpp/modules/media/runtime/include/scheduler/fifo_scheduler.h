#pragma once

/**
 * @file fifo_scheduler.h
 * @brief 先入先出调度器
 *
 * 最简单的调度策略：每次 Notify 直接下游节点的 Executor Post
 * 一个 ExecuteDrain 任务。公平、无优先级、无批处理。
 *
 * 整体数据流：
 *   [上游 Send]
 *       ↓
 *   QueueTransport::Send → MailboxPush
 *       ↓ (Accepted / DroppedOldest)
 *   NotifyCallback → IEdgeContext::TrySchedule → CAS scheduled
 *       ↓ (CAS 成功)
 *   IScheduler::Notify(edge)
 *       ↓
 *   dst_ctx->executor->Post([edge] { edge->ExecuteDrain(); })
 *       ↓
 *   ExecuteDrain: while (transport->TryReceive()) { consumer(msg); }
 *       ↓
 *   重置 scheduled → 检查剩余数据 → TrySchedule 下一轮
 *
 * 如果下游节点的执行模式是 Serialized，Drain 提交的 task 会进入
 * NodeContext 的内部队列，逐一串行执行。
 */

#include "i_scheduler.h"
#include "executor/i_executor.h"
#include "graph/edge_context.h"

namespace runtime {

class FIFOScheduler : public IScheduler {
public:
    /**
     * @brief 通知调度器执行 Drain
     *
     * 过程：
     *   1. 检查 edge 是否有效
     *   2. 获取下游 NodeContext
     *   3. Post 一个任务到下游节点的 Executor
     *   4. 任务调用 edge->ExecuteDrain()
     *
     * 注意：edge 通过 shared_ptr 捕获到 lambda 中，
     * 确保 Drain 执行时 EdgeContext 仍然存活。
     */
    bool Notify(std::shared_ptr<IEdgeContext> edge) override {
        if (!edge) {
            return false;
        }

        auto* dst_ctx = edge->GetDestination();
        if (!dst_ctx || !dst_ctx->executor) {
            return false;
        }

        // 将 Drain 任务 Post 到下游的 Executor
        return dst_ctx->executor->Post([edge = std::move(edge)]() {
            edge->ExecuteDrain();
        });
    }
};

}
