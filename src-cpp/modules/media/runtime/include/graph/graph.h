#pragma once

/**
 * @file graph.h
 * @brief 管线拓扑图——框架的核心编排器
 *
 * Graph 是用户创建管线的主要入口。它管理：
 *   - NodeContext 的集合（节点生命周期和运行时 metadata）
 *   - EdgeContext 的集合（节点之间的连接）
 *   - Scheduler 的实例（决定 Drain 的调度策略）
 *
 * 典型使用流程：
 *   // 1. 创建 Graph
 *   Graph graph;
 *
 *   // 2. 设置调度器
 *   graph.SetScheduler(std::make_shared<FIFOScheduler>());
 *
 *   // 3. 添加节点（编译期自动检测 Input/Output port 类型）
 *   graph.AddNode<RtspSource>("src", executor);
 *   graph.AddNode<Decoder>("dec", executor);
 *   graph.AddNode<Inference>("inf", executor);
 *
 *   // 4. 连接节点
 *   graph.Connect<MediaPacket>("src", "dec");
 *   graph.Connect<MediaFrame>("dec", "inf");
 *
 *   // 5. 启动管线
 *   graph.Start();
 *
 *   // ... 运行 ...
 *
 *   // 6. 停止管线
 *   graph.Stop();
 *
 * 启动顺序的严格约定：
 *   1. OpenDispatch（准备接收数据）
 *   2. Init 全部节点
 *   3. Start 全部 Executor（启动线程池）
 *   4. Start 全部节点（开始生产/处理数据）
 *   如果任何一步失败，回滚已执行的操作。
 */

#include <any>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "graph/edge_context.h"
#include "graph/node_context.h"
#include "executor/i_executor.h"
#include "node/i_node.h"
#include "port/input_port.h"
#include "port/output_port.h"
#include "scheduler/i_scheduler.h"
#include "transport/direct_transport.h"
#include "transport/i_mailbox.h"
#include "transport/queue_transport.h"

namespace runtime {

namespace detail {

/**
 * @brief 编译期检测一个类是否有 Input() 方法
 *
 * 用于 AddNode 时判断节点类型：
 *   HasInputMethod<T>::value == true → 有 Input() → 是 SinkNode 或 TransformNode
 *   HasOutputMethod<T>::value == true → 有 Output() → 是 SourceNode 或 TransformNode
 */
template<typename T, typename = void>
struct HasInputMethod : std::false_type {};

template<typename T>
struct HasInputMethod<T, std::void_t<decltype(std::declval<T>().Input())>> : std::true_type {};

template<typename T, typename = void>
struct HasOutputMethod : std::false_type {};

template<typename T>
struct HasOutputMethod<T, std::void_t<decltype(std::declval<T>().Output())>> : std::true_type {};

}

/**
 * @brief 传输通道类型
 *
 * Queue:  异步、跨线程、缓冲（默认）
 * Direct: 同步、同线程、不缓冲
 */
enum class TransportType {
    Queue,
    Direct
};

class Graph {
public:
    /**
     * @brief 添加一个节点
     *
     * 编译期自动检测节点类型：
     *   - 有 Input() → 注册输入端口到 input_ports map
     *   - 有 Output() → 注册输出端口到 output_ports map
     *
     * 节点 ID 必须唯一。如果 id 已存在，返回空字符串。
     *
     * @tparam T 节点类型（必须继承 INode + SourceNode/SinkNode/TransformNode）
     * @param id 节点唯一 ID
     * @param executor 节点使用的执行器
     * @param args 节点构造函数的额外参数
     * @return 成功时返回 id，失败时返回空字符串
     */
    template<typename T, typename... Args>
    std::string AddNode(std::string id, std::shared_ptr<IExecutor> executor, Args&&... args) {
        return AddNodeWithOptions<T>(
            std::move(id), std::move(executor), NodeOptions{}, std::forward<Args>(args)...);
    }

    /**
     * @brief 添加节点（带选项）
     *
     * NodeOptions 可用于设置 NodeExecutionMode（Serialized/Concurrent）。
     *
     * @tparam T 节点类型
     * @param id 节点唯一 ID
     * @param executor 执行器
     * @param options 节点选项（执行模式等）
     * @param args 构造参数
     * @return 成功返回 id，失败返回空字符串
     */
    template<typename T, typename... Args>
    std::string AddNodeWithOptions(std::string id,
                                   std::shared_ptr<IExecutor> executor,
                                   NodeOptions options,
                                   Args&&... args) {
        // 校验参数
        if (id.empty() || !executor || nodes_.find(id) != nodes_.end()) {
            return {};
        }

        // 创建节点实例
        auto node = std::make_shared<T>(std::forward<Args>(args)...);
        auto ctx = std::make_unique<NodeContext>();
        ctx->id = id;
        ctx->node = std::static_pointer_cast<INode>(node);
        ctx->executor = std::move(executor);
        ctx->metrics = std::make_shared<NodeMetrics>();
        ctx->execution_mode = options.execution_mode;

        // —— 编译期检测并注册输入/输出端口 ——
        // 使用 decltype 推导 port 的实际模板类型 T，
        // 这样 Connect<T> 时可以通过 type_index 匹配

        if constexpr (detail::HasInputMethod<T>::value) {
            auto& input = node->Input();
            using InType = typename std::decay_t<decltype(input)>::Type;
            ctx->input_ports[std::type_index(typeid(InType))] = &input;
        }

        if constexpr (detail::HasOutputMethod<T>::value) {
            auto& output = node->Output();
            using OutType = typename std::decay_t<decltype(output)>::Type;
            ctx->output_ports[std::type_index(typeid(OutType))] = &output;
        }

        auto [_, inserted] = nodes_.emplace(id, std::move(ctx));
        return inserted ? id : std::string{};
    }

    /**
     * @brief 连接两个节点
     *
     * 创建一个 EdgeContext，包含：
     *   - Transport（QueueTransport 或 DirectTransport）
     *   - 上下游的 metrics 关联
     *   - scheduler 的 weak_ptr
     *   - consumer_（InputPort::Receive 的包装）
     *
     * Transport 类型决定了数据传输方式：
     *   Queue:  异步，通过 Mailbox + Drain 循环
     *   Direct: 同步，Send 直接调用下游 Dispatch
     *
     * 统计回调注册（仅 QueueTransport）：
     *   每次 Send 结果被记录到 dst_ctx.metrics 中，
     *   区分 Accepted / DroppedOldest / DroppedNewest / Closed。
     *
     * @tparam T 传输的数据类型（必须和上下游的 port 类型匹配）
     * @param src 源节点 ID
     * @param dst 目标节点 ID
     * @param type 传输通道类型（Queue / Direct）
     * @param capacity QueueTransport 的 Mailbox 容量
     * @param policy 背压策略
     * @return 成功返回 true，失败返回 false
     */
    template<typename T>
    bool Connect(const std::string& src, const std::string& dst,
                 TransportType type = TransportType::Queue,
                 std::size_t capacity = 64,
                 BackpressurePolicy policy = BackpressurePolicy::DropOldest) {
        auto src_it = nodes_.find(src);
        auto dst_it = nodes_.find(dst);
        if (src_it == nodes_.end() || dst_it == nodes_.end() || !scheduler_) {
            return false;
        }

        auto& src_ctx = *src_it->second;
        auto& dst_ctx = *dst_it->second;

        // 通过 type_index 匹配端口类型
        auto out_it = src_ctx.output_ports.find(std::type_index(typeid(T)));
        auto in_it = dst_ctx.input_ports.find(std::type_index(typeid(T)));
        if (out_it == src_ctx.output_ports.end() || in_it == dst_ctx.input_ports.end()) {
            return false;
        }

        auto* output = std::any_cast<OutputPort<T>*>(out_it->second);
        auto* input = std::any_cast<InputPort<T>*>(in_it->second);
        if (!output || !input) {
            return false;
        }

        // 创建 EdgeContext
        auto edge = std::make_shared<EdgeContext<T>>();
        edge->edge_id = src + "->" + dst;
        edge->src_id = src;
        edge->dst_ = &dst_ctx;
        edge->dst_metrics_ = dst_ctx.metrics;
        edge->scheduler_ = scheduler_;

        // 创建 Transport
        if (type == TransportType::Direct) {
            edge->transport = std::make_shared<DirectTransport<T>>();
        } else {
            edge->transport = std::make_shared<QueueTransport<T>>(capacity, policy);
        }

        // —— 统计回调 ——
        // 每次 Send 结果更新 dst 节点的 metrics
        auto count_send_result = [metrics = dst_ctx.metrics](MailboxPushResult result) {
            if (!metrics) {
                return;
            }

            switch (result) {
            case MailboxPushResult::Accepted:
                metrics->enqueued.fetch_add(1);
                break;
            case MailboxPushResult::DroppedOldest:
                metrics->enqueued.fetch_add(1);   // 数据最终入队了
                metrics->dropped.fetch_add(1);     // 但一个旧数据被丢弃
                break;
            case MailboxPushResult::DroppedNewest:
                metrics->dropped.fetch_add(1);     // 新数据被丢弃
                break;
            case MailboxPushResult::Closed:
                metrics->rejected.fetch_add(1);    // 队列已关闭
                break;
            }
        };

        // QueueTransport 专属设置：通知回调 + 统计回调
        if (auto queue_transport = std::dynamic_pointer_cast<QueueTransport<T>>(edge->transport)) {
            queue_transport->SetSendResultCallback(count_send_result);
            std::weak_ptr<IEdgeContext> weak_edge = edge;
            queue_transport->SetNotifyCallback([weak_edge]() {
                if (auto edge = weak_edge.lock()) {
                    edge->TrySchedule();
                }
            });
        }

        // DirectTransport 专属设置：consumer（直接同步调用）
        if (auto direct_transport = std::dynamic_pointer_cast<DirectTransport<T>>(edge->transport)) {
            direct_transport->SetSendResultCallback(count_send_result);
            direct_transport->SetConsumer([input, dst = &dst_ctx, metrics = dst_ctx.metrics](T msg) mutable {
                dst->Dispatch([input, metrics, msg = std::move(msg)]() mutable {
                        try {
                            input->Receive(std::move(msg));
                            if (metrics) {
                                metrics->processed.fetch_add(1);
                            }
                        } catch (...) {
                            if (metrics) {
                                metrics->errors.fetch_add(1);
                            }
                        }
                    });
            });
        }

        // 设置 consumer_（Drain 端用于将数据传给 InputPort::Receive）
        edge->consumer_ = [input](T msg) {
            input->Receive(std::move(msg));
        };

        // 将 Transport 注册到上游 OutputPort
        output->AddTransport(edge->transport);
        edges_.push_back(std::move(edge));
        return true;
    }

    /// @brief 设置调度器
    void SetScheduler(std::shared_ptr<IScheduler> scheduler) {
        scheduler_ = std::move(scheduler);
    }

    /// @brief 获取节点上下文（用于调试/监控）
    NodeContext* GetNode(const std::string& id) {
        auto it = nodes_.find(id);
        return it != nodes_.end() ? it->second.get() : nullptr;
    }

    /**
     * @brief 启动整个管线
     *
     * 严格的启动顺序：
     *   1. OpenDispatch（所有节点队列设为可接收）
     *   2. Init 全部节点（分配资源）
     *   3. Start 全部 Executor（启动线程池）
     *   4. Start 全部节点（开始处理）
     *
     * 失败回滚：
     *   - Executor.Start 失败时，Stop 已 Start 的 Executor
     *   - Node.Start 失败时，调用 Stop 回滚所有步骤
     */
    bool Start() {
        if (running_) {
            return false;
        }

        // 1. 打开所有节点的分发队列
        for (auto& [_, ctx] : nodes_) {
            ctx->OpenDispatch();
        }

        // 2. Init 所有节点
        for (auto& [_, ctx] : nodes_) {
            if (ctx->node && !ctx->node->Init()) {
                return false;
            }
        }

        // 3. Start 所有 Executor（去重：多个节点可能共享一个 Executor）
        std::unordered_set<IExecutor*> started;
        for (auto& [_, ctx] : nodes_) {
            if (ctx->executor && started.insert(ctx->executor.get()).second) {
                if (!ctx->executor->Start()) {
                    // 回滚已启动的 Executor
                    StopStartedExecutors(started);
                    return false;
                }
            }
        }

        // 4. Start 所有节点
        for (auto& [_, ctx] : nodes_) {
            if (ctx->node && !ctx->node->Start()) {
                Stop();
                return false;
            }
        }

        running_ = true;
        return true;
    }

    /**
     * @brief 停止整个管线
     *
     * 严格的停止顺序（与 Start 相反）：
     *   1. Stop 所有节点（停止生产/处理）
     *   2. Close 所有边（关闭 Transport）
     *   3. CloseDispatch 所有节点（拒绝新任务）
     *   4. Stop 所有 Executor（停止线程）
     *   5. Deinit 所有节点（释放资源）
     */
    void Stop() {
        // 1. Stop 节点
        for (auto& [_, ctx] : nodes_) {
            if (ctx->node) {
                ctx->node->Stop();
            }
        }

        // 2. Close 边
        for (auto& edge : edges_) {
            edge->Close();
        }

        // 3. Stop dispatch
        for (auto& [_, ctx] : nodes_) {
            ctx->CloseDispatch();
        }

        // 4. Stop Executor（去重）
        std::unordered_set<IExecutor*> stopped;
        for (auto& [_, ctx] : nodes_) {
            if (ctx->executor && stopped.insert(ctx->executor.get()).second) {
                ctx->executor->Stop();
            }
        }

        // 5. Deinit 节点
        for (auto& [_, ctx] : nodes_) {
            if (ctx->node) {
                ctx->node->Deinit();
            }
        }

        running_ = false;
    }

    /// @brief 获取节点度量的快照
    bool GetMetrics(const std::string& id, NodeMetricsSnapshot& out) const {
        auto it = nodes_.find(id);
        if (it == nodes_.end() || !it->second->metrics) return false;
        out = it->second->metrics->Snapshot();
        return true;
    }

private:
    /// @brief 回滚已启动的 Executor（当 Start 过程失败时）
    void StopStartedExecutors(const std::unordered_set<IExecutor*>& started) {
        for (auto* executor : started) {
            if (executor) {
                executor->Stop();
            }
        }
    }

    std::unordered_map<std::string, std::unique_ptr<NodeContext>> nodes_;  ///< 节点 Map
    std::vector<std::shared_ptr<IEdgeContext>> edges_;                     ///< 边集合
    std::shared_ptr<IScheduler> scheduler_;                                ///< 调度器
    bool running_{false};                                                  ///< 运行状态
};

}
