#pragma once

/**
 * @file edge_context.h
 * @brief 边的运行时上下文（连接两个节点的通道的管理单元）
 *
 * EdgeContext 是"边"的抽象，连接上游 OutputPort 和下游 InputPort。
 * 每个 Graph::Connect 产生一个 EdgeContext 实例，其中包含：
 *   - Transport：实际的数据通道
 *   - consumer_：将数据传入下游 InputPort 的函数
 *   - scheduled 原子标志：防止重复的 Drain 任务 Post
 *
 * 核心操作：ExecuteDrain 驱动数据消费
 *   1. 循环调用 transport->TryReceive() 消费数据
 *   2. 每个数据通过 dst->Dispatch(consumer_, msg) 提交给下游节点
 *   3. ScheduleReset RAII 在函数退出时自动 reset scheduled 标志
 *   4. 如果退出时 transport 还有数据，重新 TrySchedule
 *
 * scheduled 标志的设计意图：
 *   当 CAS scheduled 从 false→true 成功后，才会向 Executor Post
 *   Drain 任务。如果已经有一个 Drain 任务在排队或执行中，后续的
 *   Notify 调用直接忽略（不竞争）。Drain 执行完毕后 reset 标志，
 *   并检查是否有更多数据，有则重新 TrySchedule 开始新一轮。
 *
 * 线程安全：
 *   ExecuteDrain 可能被多个 Notify 触发，但通过 scheduled CAS
 *   保证同一时刻只有一个 Drain 在执行。consumer_ 调用链会进入
 *   NodeContext::Dispatch，内部由 mutex 或直接 Post 保护。
 */

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <string>

#include "graph/node_context.h"
#include "scheduler/i_scheduler.h"
#include "transport/i_transport.h"

namespace runtime {

/**
 * @brief 节点边缘上下文的非模板基类
 *
 * 作用：
 *   1. 持有 scheduled CAS 标志（类型无关）
 *   2. 包含指向下游 NodeContext 的指针
 *   3. 包含 scheduler 的 weak_ptr（避免循环引用）
 *   4. 提供 TrySchedule 的通用实现
 *
 * scheduled 原子变量：
 *   false：可以尝试调度 Drain
 *   true：已有 Drain 在 Executor 中或正在执行
 *
 * scheduled 被放置在基类中是因为它不涉及 T 类型，
 * 调度器可以直接操作 IEdgeContext 而不需要知道 T 是什么。
 */
class IEdgeContext : public std::enable_shared_from_this<IEdgeContext> {
public:
    virtual ~IEdgeContext() = default;

    /// @brief 执行数据排空（取 Transport 中的数据送到下游）
    virtual void ExecuteDrain() = 0;

    /// @brief 关闭（关闭底层 Transport）
    virtual void Close() = 0;

    /// @brief 检查 Transport 是否为空
    virtual bool Empty() const = 0;

    /// @brief 获取下游 NodeContext
    virtual NodeContext* GetDestination() const = 0;

    /**
     * @brief 尝试调度一次 Drain
     *
     * CAS 过程：
     *   1. 尝试 scheduled 从 false→true（期望 false，设 true）
     *   2. 如果 CAS 失败（scheduled==true），说明已有 Drain 在途，
     *      直接返回 true（不需要重复调度）
     *   3. CAS 成功后，尝试从 scheduler_ weak_ptr lock 获取 scheduler
     *   4. 调用 scheduler->Notify(shared_from_this()) 提交 Drain
     *   5. 如果失败（scheduler 已销毁或 Notify 返回 false），
     *      reset scheduled 并计一次 rejected
     *
     * @return true 调度成功或有 Drain 已在途中，false 调度失败
     */
    bool TrySchedule() {
        bool expected = false;
        // CAS：期望当前是 false，设置为 true
        if (!scheduled.compare_exchange_strong(expected, true)) {
            return true;  // 已有 Drain 在途中
        }

        // 获取 scheduler（可能被销毁）
        auto scheduler = scheduler_.lock();
        if (!scheduler || !scheduler->Notify(shared_from_this())) {
            // 调度失败，恢复 scheduled 并计 rejected
            scheduled.store(false);
            if (auto metrics = dst_metrics_.lock()) {
                metrics->rejected.fetch_add(1);
            }
            return false;
        }

        return true;
    }

    std::atomic<bool> scheduled{false};    ///< 防止重复调度 Drain
    NodeContext* dst_{nullptr};             ///< 下游节点上下文
    std::weak_ptr<NodeMetrics> dst_metrics_;  ///< 下游节点度量
    std::weak_ptr<IScheduler> scheduler_;  ///< 调度器（weak 避免循环引用）
    std::string edge_id;                   ///< 边标识（"src->dst"）
    std::string src_id;                    ///< 源节点 ID
};

/// 消费函数类型（即下游 InputPort::Receive 的包装）
template<typename T>
using Consumer = std::function<void(T)>;

/**
 * @brief 模板化的边上下文（持有具体类型的 Transport）
 *
 * @tparam T 数据类型
 */
template<typename T>
class EdgeContext : public IEdgeContext {
public:
    std::shared_ptr<ITransport<T>> transport;  ///< 数据通道
    Consumer<T> consumer_;                     ///< 下游消费函数

    NodeContext* GetDestination() const override {
        return dst_;
    }

    /// @brief 关闭 Transport
    void Close() override {
        if (transport) {
            transport->Close();
        }
    }

    bool Empty() const override {
        return !transport || transport->Empty();
    }

    /**
     * @brief 执行数据排空
     *
     * 设计要点：
     *
     * 1. ScheduleReset RAII
     *    ~ScheduleReset 时：
     *      a. 重置 scheduled = false
     *      b. 检查 transport 是否还有数据
     *      c. 有数据则调用 TrySchedule() 启动新一轮 Drain
     *    这确保不管 Drain 是正常执行完还是中途异常退出，
     *    scheduled 标志都会被重置，不会"死锁"在这个边上。
     *
     * 2. 非阻塞循环
     *    while 循环不断 TryReceive 直到返回 nullopt（队列空）。
     *    不等待——这是设计意图：Drain 执行一次，尽可能多地消费数据，
     *    然后退出让调度器决定何时再调度。这避免了 ExecuteDrain
     *    长时间阻塞在某个边上。
     *
     * 3. 数据提交
     *    每个数据通过 dst->Dispatch 提交给下游节点。
     *    Dispatch 内部会处理序列化/并发。
     *    在 Dispatch 的 lambda 中，consumer 被移动捕获，
     *    metrics 以 shared_ptr 的值捕获确保计数更新正确。
     *
     * 4. 异常安全
     *    consumer(msg) 可能抛出异常，被 try-catch 捕获，
     *    计入 metrics->errors 并继续处理下一个消息。
     */
    void ExecuteDrain() override {
        // RAII：退出时重置 scheduled，如有剩余数据则重新调度
        struct ScheduleReset {
            EdgeContext* edge;
            ~ScheduleReset() {
                edge->scheduled.store(false);
                if (edge->transport && !edge->transport->Empty()) {
                    edge->TrySchedule();
                }
            }
        } reset{this};

        // 循环消费直到队列空
        while (transport) {
            auto msg = transport->TryReceive();
            if (!msg.has_value()) {
                break;  // 队列空
            }

            auto* dst = GetDestination();
            if (!dst) {
                // 下游 NodeContext 已被销毁，计 rejected
                if (auto metrics = dst_metrics_.lock()) {
                    metrics->rejected.fetch_add(1);
                }
                continue;
            }

            // 提交到下游节点的 Dispatch 队列
            dst->Dispatch([consumer = consumer_,
                           metrics = dst_metrics_,
                           msg = std::move(*msg)]() mutable {
                    try {
                        if (consumer) {
                            consumer(std::move(msg));
                        }
                        if (auto locked = metrics.lock()) {
                            locked->processed.fetch_add(1);
                        }
                    } catch (...) {
                        if (auto locked = metrics.lock()) {
                            locked->errors.fetch_add(1);
                        }
                    }
                });
        }
    }
};

}
