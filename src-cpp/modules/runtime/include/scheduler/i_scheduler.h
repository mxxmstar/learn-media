#pragma once

/**
 * @file i_scheduler.h
 * @brief 调度器接口
 *
 * 调度器是"边→节点"的 Drain 调度策略抽象。
 *
 * 当上游 Send 数据到 Transport 后，Transport 调用 NotifyCallback，
 * 然后 IEdgeContext::TrySchedule() 通过 CAS + weak_ptr 安全地
 * 调用 scheduler->Notify(edge_shared_ptr)。
 *
 * 调度器的职责：
 *   接收 Notify(edge) 后，决定如何在 Executor 上调度 ExecuteDrain。
 *
 * 当前实现：
 *   FIFOScheduler：每次 Notify 直接 Post 一个 Drain 任务到下游节点的
 *                  Executor。简单、公平、无优先级。
 *
 * 未来可以扩展的调度策略：
 *   - PriorityScheduler：按边/端口的优先级调度
 *   - BatchScheduler：多条边积攒一批后再调度
 *   - FrameDropScheduler：自定义丢帧策略
 *
 * 为什么 scheduler 是独立的组件而不是直接在 EdgeContext 中 Post？
 *   分离后不同的调度策略可以复用同一个 EdgeContext 和 Transport 实现。
 */

#include <memory>

namespace runtime {

class IEdgeContext;

class IScheduler {
public:
    virtual ~IScheduler() = default;

    /**
     * @brief 通知调度器：有一条边有数据可消费
     * @param edge 有数据待消费的边
     * @return true 成功提交 Drain 任务，false 提交失败
     *
     * 实现通常：
     *   1. 从 edge->GetDestination() 获取下游 NodeContext
     *   2. 从 NodeContext->executor 获取执行器
     *   3. Post 一个 lambda 执行 edge->ExecuteDrain()
     */
    virtual bool Notify(std::shared_ptr<IEdgeContext> edge) = 0;
};

}
