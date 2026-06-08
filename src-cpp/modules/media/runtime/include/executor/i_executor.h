#pragma once

/**
 * @file i_executor.h
 * @brief 执行器接口
 *
 * Executor 是"任务的执行引擎"抽象。框架不关心任务在哪执行——
 * 可以是一个线程池、一个单独线程、甚至是主线程。
 *
 * 职责：
 *   - Start()：启动执行器（分配线程/资源）
 *   - Post(task)：提交一个任务到执行队列
 *   - Stop()：停止执行器（等待所有任务完成）
 *
 * IExecutor 被 Graph 中的每个 NodeContext 持有。
 * 多个节点可以共享同一个 Executor（同一个线程池）。
 *
 * 并发模型：
 *   - 一个管线有多个 Executor 实例
 *   - 每个 Executor 对应一组线程（如推理线程池、IO 线程池）
 *   - 节点绑定到特定 Executor，确保同类任务在正确的线程上执行
 */

#include <functional>

namespace runtime {

class IExecutor {
public:
    using Task = std::function<void()>;

    virtual ~IExecutor() = default;

    /// @brief 启动执行器（开始接受任务）
    /// @return true 成功，false 失败
    virtual bool Start() = 0;

    /// @brief 停止执行器（等待所有正在执行的任务完成）
    virtual void Stop() = 0;

    /**
     * @brief 提交任务到执行器
     * @param task 可调用对象
     * @return true 提交成功，false 提交失败（执行器已停止）
     *
     * 实现必须保证：
     *   - Post 返回后 task 会被异步执行（或已被拒绝）
     *   - task 被执行时不会抛出未捕获的异常（Post 内部负责 catch）
     *   - 在 Stop() 返回后，所有已提交的 task 已被执行或丢弃
     */
    virtual bool Post(Task task) = 0;
};

}
