#pragma once

/**
 * @file dropframe_scheduler.h
 * @brief 丢帧调度器（预留的丢帧策略钩子）
 *
 * 当前实现与 FIFOScheduler 完全一致。
 * 设计意图是作为自定义丢帧逻辑的扩展点。
 *
 * 可能的未来实现：
 *   在 ExecuteDrain 前先检查下游节点的 pending 队列长度，
 *   如果超过阈值，跳过这次 Drain 并丢弃队列中的数据。
 *
 * 用法：
 *   当管线需要"如果下游忙就直接丢帧"时使用：
 *     graph.SetScheduler(std::make_shared<DropFrameScheduler>(max_delay_ms));
 *
 * @todo 实现差异化的丢帧逻辑
 */

#include "i_scheduler.h"
#include "executor/i_executor.h"
#include "graph/edge_context.h"

namespace runtime {

class DropFrameScheduler : public IScheduler {
public:
    /**
     * @brief 当前实现等同于 FIFOScheduler
     *
     * 后续可以在这里加入：
     *   - 检查 downstream 队列深度
     *   - 丢弃旧帧（只保留最新几帧）
     *   - 基于时间戳的丢帧
     */
    bool Notify(std::shared_ptr<IEdgeContext> edge) override {
        if (!edge) {
            return false;
        }

        auto* dst_ctx = edge->GetDestination();
        if (!dst_ctx || !dst_ctx->executor) {
            return false;
        }

        return dst_ctx->executor->Post([edge = std::move(edge)]() {
            edge->ExecuteDrain();
        });
    }
};

}
