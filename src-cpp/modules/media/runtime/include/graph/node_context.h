#pragma once

/**
 * @file node_context.h
 * @brief 节点的运行时上下文
 *
 * NodeContext 是 Graph 内部使用的节点封装。它将"用户节点"
 *（实现了 INode + 某种数据处理 mixin）与运行时基础设施绑定：
 *   - Executor：节点在哪组线程上执行
 *   - Metrics：节点级别的数据流统计
 *   - Dispatch 机制：控制 Process 调用的并发方式
 *
 * Dispatch 有两种模式：
 *   - Serialized（默认）：多个上游同时发数据时，通过内部队列
 *     确保 Process 顺序执行，天然线程安全
 *   - Concurrent：每个数据直接 Post 到 Executor，Process
 *     可能并发执行（需要子类自己处理线程安全）
 *
 * 序列化 Dispatch 的设计：
 *   Dispatch_queue_ 是一个 std::deque<Task>，受 mutex 保护。
 *   dispatch_active_ 标记当前是否有一个"泵"任务在 Executor 中排队。
 *   这个设计确保任何时候 Executor 中最多只有一个 Drain 任务属于
 *   这个节点，避免并发消费。
 */

#include <any>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "executor/i_executor.h"

namespace runtime {

class INode;

/**
 * @brief 节点度量快照（非原子读取，用于监控展示）
 *
 * 定时从 NodeMetrics 的 atomic 字段 snapshot 出来，
 * 避免调用者一直持有 atomic 引用。
 */
struct NodeMetricsSnapshot {
    std::uint64_t enqueued{0};   ///< 入队数据量（来自上游 Send）
    std::uint64_t processed{0};  ///< 已处理数据量（Process 完成）
    std::uint64_t dropped{0};    ///< 丢弃数据量（背压 DropOldest/DropNewest）
    std::uint64_t rejected{0};   ///< 拒绝数据量（通道关闭/系统忙）
    std::uint64_t errors{0};     ///< 异常数据量（Process 抛出异常）
};

/**
 * @brief 节点度量（原子计数器）
 *
 * 每个 NodeContext 有一个 metrics 实例，由 Graph::Connect
 * 注册的 SendResultCallback 和 EdgeContext::ExecuteDrain 更新。
 */
struct NodeMetrics {
    std::atomic<std::uint64_t> enqueued{0};   ///< Graph::Connect 注册的回调更新
    std::atomic<std::uint64_t> processed{0};  ///< ExecuteDrain 中 Process 成功后更新
    std::atomic<std::uint64_t> dropped{0};    ///< DroppedOldest/DroppedNewest 时更新
    std::atomic<std::uint64_t> rejected{0};   ///< 队列关闭或系统忙时更新
    std::atomic<std::uint64_t> errors{0};     ///< Process 抛出异常时更新

    /// @brief 获取当前快照
    NodeMetricsSnapshot Snapshot() const {
        return {
            enqueued.load(),
            processed.load(),
            dropped.load(),
            rejected.load(),
            errors.load()
        };
    }
};

/**
 * @brief 节点执行模式
 *
 * Serialized:  通过内部队列确保 Process 顺序调用（默认，线程安全）
 * Concurrent:  直接 Post 到 Executor，Process 可能多线程并发
 */
enum class NodeExecutionMode {
    Serialized,
    Concurrent
};

/**
 * @brief 节点选项（Graph::AddNodeWithOptions 时传入）
 */
struct NodeOptions {
    NodeExecutionMode execution_mode{NodeExecutionMode::Serialized};
};

/**
 * @brief 节点运行时上下文
 *
 * 每个通过 Graph::AddNode 注册的节点都会有一个对应的 NodeContext。
 * 它不直接暴露给用户代码，而是被 Graph、EdgeContext、Scheduler 访问。
 */
struct NodeContext {
    using Task = IExecutor::Task;

    std::string id;                                    ///< 节点唯一标识
    std::shared_ptr<INode> node;                       ///< 用户实现的节点实例
    std::shared_ptr<IExecutor> executor;               ///< 节点绑定的执行器
    std::shared_ptr<NodeMetrics> metrics;              ///< 节点度量
    NodeExecutionMode execution_mode{NodeExecutionMode::Serialized};  ///< 执行模式

    /// @brief 按类型存储的输入端口指针（Graph::AddNode 时自动注册）
    std::unordered_map<std::type_index, std::any> input_ports;

    /// @brief 按类型存储的输出端口指针（Graph::AddNode 时自动注册）
    std::unordered_map<std::type_index, std::any> output_ports;

    /**
     * @brief 分发一个任务给此节点执行
     *
     * Serialized 模式：
     *   1. 将 task 入队 dispatch_queue_
     *   2. 如果 dispatch_active_ == false，设置 active 并 Post 到 Executor
     *   3. Executor 执行 RunDispatchOne：弹出一个 task 执行
     *   4. 执行完后检查队列是否还有剩余，有则 ScheduleDispatch 继续
     *
     * Concurrent 模式：
     *   直接 Post 到 Executor，每个 task 独立调度
     *
     * @param task 可调用对象（通常是 ExecuteDrain 中 lambda：consumer + metrics）
     * @return true 提交成功，false 拒绝
     */
    bool Dispatch(Task task) {
        // —— 并发模式：直接提交 ——
        if (execution_mode == NodeExecutionMode::Concurrent) {
            if (!executor || !executor->Post(std::move(task))) {
                CountRejected();
                return false;
            }
            return true;
        }

        // —— 串行模式：入队 + 按需调度 ——
        bool should_schedule = false;
        {
            std::lock_guard<std::mutex> lock(dispatch_mutex_);
            if (dispatch_closed_) {
                CountRejected();
                return false;
            }

            dispatch_queue_.push_back(std::move(task));

            // 只有当前没有排队的 active 任务时才需要新 Post 一个
            if (!dispatch_active_) {
                dispatch_active_ = true;
                should_schedule = true;
            }
        }

        if (!should_schedule) {
            return true;  // 已有 active 任务会在完成后自动调度下一个
        }

        return ScheduleDispatch();
    }

    /// @brief 关闭分发队列（Stop 阶段调用）
    void CloseDispatch() {
        std::lock_guard<std::mutex> lock(dispatch_mutex_);
        dispatch_closed_ = true;
        dispatch_queue_.clear();
    }

    /// @brief 重新打开分发队列（Start 阶段调用）
    void OpenDispatch() {
        std::lock_guard<std::mutex> lock(dispatch_mutex_);
        dispatch_closed_ = false;
        dispatch_active_ = false;  // 重置 active 状态，允许新的调度序列
        dispatch_queue_.clear();
    }

private:
    /**
     * @brief 将"泵"任务 Post 到 Executor
     *
     * 这个任务执行 RunDispatchOne，它：
     *   1. 从 dispatch_queue_ 弹出一个 task
     *   2. 执行 task（Process 调用链）
     *   3. 检查队列是否有后续任务
     *   4. 有则递归 ScheduleDispatch，无则设置 dispatch_active_ = false
     */
    bool ScheduleDispatch() {
        if (!executor || !executor->Post([this]() { RunDispatchOne(); })) {
            // Post 失败：恢复 active 状态
            std::lock_guard<std::mutex> lock(dispatch_mutex_);
            dispatch_active_ = false;
            CountRejected();
            return false;
        }

        return true;
    }

    /**
     * @brief 执行一个排队任务（由 Executor 线程调用）
     *
     * 关键设计点：
     *   - 在 task() 执行时释放了 mutex，避免持有锁执行用户代码
     *   - task() 异常被捕获并计为 error
     *   - task() 执行完后重新加锁检查队列剩余
     *   - 如果队列还有更多任务，继续 ScheduleDispatch（链式泵送）
     */
    void RunDispatchOne() {
        Task task;
        {
            std::lock_guard<std::mutex> lock(dispatch_mutex_);
            if (dispatch_queue_.empty()) {
                dispatch_active_ = false;
                return;
            }

            task = std::move(dispatch_queue_.front());
            dispatch_queue_.pop_front();
        }

        try {
            task();
        } catch (...) {
            if (metrics) {
                metrics->errors.fetch_add(1);
            }
        }

        // 检查是否还有更多任务
        bool has_more = false;
        {
            std::lock_guard<std::mutex> lock(dispatch_mutex_);
            has_more = !dispatch_queue_.empty();
            if (!has_more) {
                dispatch_active_ = false;
            }
        }

        if (has_more) {
            ScheduleDispatch();
        }
    }

    void CountRejected() {
        if (metrics) {
            metrics->rejected.fetch_add(1);
        }
    }

    std::mutex dispatch_mutex_;           ///< 保护 dispatch_queue_ 和状态变量
    std::deque<Task> dispatch_queue_;     ///< 串行模式的任务队列
    bool dispatch_active_{false};         ///< 是否有"泵"任务在 Executor 中排队
    bool dispatch_closed_{false};         ///< 队列是否已关闭（Stop 后不接收新任务）
};

}
