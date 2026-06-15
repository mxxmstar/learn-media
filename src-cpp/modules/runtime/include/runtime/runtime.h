#pragma once

/**
 * @file runtime.h
 * @brief 运行时顶层封装——用户的唯一入口
 *
 * Runtime 是框架对外暴露的"一站式"入口类。
 * 用户只需创建 Runtime 实例，然后用它创建 Executor、
 * 添加节点、连接管线、启动/停止。
 *
 * 典型用法：
 * @code
 *   runtime::Runtime rt;
 *
 *   auto exec = rt.CreateExecutor("main", "general", 4);
 *
 *   auto& graph = rt.GetGraph();
 *   auto src = graph.AddNode<RtspSource>("source", exec, "rtsp://...");
 *   auto dec = graph.AddNode<Decoder>("decoder", exec);
 *   graph.Connect<MediaPacket>(src, dec);
 *
 *   rt.Start();
 *   // ... wait ...
 *   rt.Stop();
 *   rt.ShutdownAllPools();
 * @endcode
 *
 * Runtime 内部自动设置了 FIFOScheduler 作为默认调度器。
 */

#include <memory>
#include <vector>

#include "graph/graph.h"
#include "scheduler/fifo_scheduler.h"
#include "executor/asio_executor.h"

namespace runtime {

class Runtime {
public:
    Runtime() {
        // 默认使用 FIFOScheduler（可直接替换为 DropFrameScheduler 等）
        graph_.SetScheduler(std::make_shared<FIFOScheduler>());
    }

    /// @brief 获取 Graph 引用（用于 AddNode / Connect）
    Graph& GetGraph() {
        return graph_;
    }

    /**
     * @brief 创建 AsioExecutor
     * @param name      执行器名称（标识用）
     * @param pool_name 线程池名称（"general", "inference", "io" 等）
     * @param pool_size 线程数（0 = 使用硬件并发数）
     * @return 执行器 shared_ptr
     *
     * 创建的执行器会被 Runtime 内部跟踪，确保生命周期。
     */
    std::shared_ptr<AsioExecutor> CreateExecutor(const std::string& name,
                                                  const std::string& pool_name = "general",
                                                  std::size_t pool_size = 0) {
        auto exec = std::make_shared<AsioExecutor>(name, pool_name, pool_size);
        executors_.push_back(exec);
        return exec;
    }

    /// @brief 启动管线（代理 Graph::Start）
    bool Start() {
        return graph_.Start();
    }

    /// @brief 停止管线（代理 Graph::Stop）
    void Stop() {
        graph_.Stop();
    }

    /// @brief 关闭所有全局线程池（程序退出时调用）
    void ShutdownAllPools() {
        AsioExecutor::StopAllPools();
    }

private:
    Graph graph_;                                    ///< 管线拓扑
    std::vector<std::shared_ptr<IExecutor>> executors_;  ///< 执行器列表
};

}
