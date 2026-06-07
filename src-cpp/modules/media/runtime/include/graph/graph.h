#pragma once

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

template<typename T, typename = void>
struct HasInputMethod : std::false_type {};

template<typename T>
struct HasInputMethod<T, std::void_t<decltype(std::declval<T>().Input())>> : std::true_type {};

template<typename T, typename = void>
struct HasOutputMethod : std::false_type {};

template<typename T>
struct HasOutputMethod<T, std::void_t<decltype(std::declval<T>().Output())>> : std::true_type {};

}

enum class TransportType {
    Queue,
    Direct
};

class Graph {
public:
    template<typename T, typename... Args>
    std::string AddNode(std::string id, std::shared_ptr<IExecutor> executor, Args&&... args) {
        return AddNodeWithOptions<T>(
            std::move(id), std::move(executor), NodeOptions{}, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    std::string AddNodeWithOptions(std::string id,
                                   std::shared_ptr<IExecutor> executor,
                                   NodeOptions options,
                                   Args&&... args) {
        if (id.empty() || !executor || nodes_.find(id) != nodes_.end()) {
            return {};
        }

        auto node = std::make_shared<T>(std::forward<Args>(args)...);
        auto ctx = std::make_unique<NodeContext>();
        ctx->id = id;
        ctx->node = std::static_pointer_cast<INode>(node);
        ctx->executor = std::move(executor);
        ctx->metrics = std::make_shared<NodeMetrics>();
        ctx->execution_mode = options.execution_mode;

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

        auto edge = std::make_shared<EdgeContext<T>>();
        edge->edge_id = src + "->" + dst;
        edge->src_id = src;
        edge->dst_ = &dst_ctx;
        edge->dst_metrics_ = dst_ctx.metrics;
        edge->scheduler_ = scheduler_;

        if (type == TransportType::Direct) {
            edge->transport = std::make_shared<DirectTransport<T>>();
        } else {
            edge->transport = std::make_shared<QueueTransport<T>>(capacity, policy);
        }

        auto count_send_result = [metrics = dst_ctx.metrics](MailboxPushResult result) {
            if (!metrics) {
                return;
            }

            switch (result) {
            case MailboxPushResult::Accepted:
                metrics->enqueued.fetch_add(1);
                break;
            case MailboxPushResult::DroppedOldest:
                metrics->enqueued.fetch_add(1);
                metrics->dropped.fetch_add(1);
                break;
            case MailboxPushResult::DroppedNewest:
                metrics->dropped.fetch_add(1);
                break;
            case MailboxPushResult::Closed:
                metrics->rejected.fetch_add(1);
                break;
            }
        };

        if (auto queue_transport = std::dynamic_pointer_cast<QueueTransport<T>>(edge->transport)) {
            queue_transport->SetSendResultCallback(count_send_result);
            std::weak_ptr<IEdgeContext> weak_edge = edge;
            queue_transport->SetNotifyCallback([weak_edge]() {
                if (auto edge = weak_edge.lock()) {
                    edge->TrySchedule();
                }
            });
        }

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

        edge->consumer_ = [input](T msg) {
            input->Receive(std::move(msg));
        };

        output->AddTransport(edge->transport);
        edges_.push_back(std::move(edge));
        return true;
    }

    void SetScheduler(std::shared_ptr<IScheduler> scheduler) {
        scheduler_ = std::move(scheduler);
    }

    NodeContext* GetNode(const std::string& id) {
        auto it = nodes_.find(id);
        return it != nodes_.end() ? it->second.get() : nullptr;
    }

    bool Start() {
        if (running_) {
            return false;
        }

        for (auto& [_, ctx] : nodes_) {
            ctx->OpenDispatch();
            if (ctx->node && !ctx->node->Init()) {
                return false;
            }
        }

        std::unordered_set<IExecutor*> started;
        for (auto& [_, ctx] : nodes_) {
            if (ctx->executor && started.insert(ctx->executor.get()).second) {
                if (!ctx->executor->Start()) {
                    StopStartedExecutors(started);
                    return false;
                }
            }
        }

        for (auto& [_, ctx] : nodes_) {
            if (ctx->node && !ctx->node->Start()) {
                Stop();
                return false;
            }
        }

        running_ = true;
        return true;
    }

    void Stop() {
        for (auto& [_, ctx] : nodes_) {
            if (ctx->node) {
                ctx->node->Stop();
            }
        }

        for (auto& edge : edges_) {
            edge->Close();
        }

        for (auto& [_, ctx] : nodes_) {
            ctx->CloseDispatch();
        }

        std::unordered_set<IExecutor*> stopped;
        for (auto& [_, ctx] : nodes_) {
            if (ctx->executor && stopped.insert(ctx->executor.get()).second) {
                ctx->executor->Stop();
            }
        }

        for (auto& [_, ctx] : nodes_) {
            if (ctx->node) {
                ctx->node->Deinit();
            }
        }

        running_ = false;
    }

    bool GetMetrics(const std::string& id, NodeMetricsSnapshot& out) const {
        auto it = nodes_.find(id);
        if (it == nodes_.end() || !it->second->metrics) return false;
        out = it->second->metrics->Snapshot();
        return true;
    }

private:
    void StopStartedExecutors(const std::unordered_set<IExecutor*>& started) {
        for (auto* executor : started) {
            if (executor) {
                executor->Stop();
            }
        }
    }

    std::unordered_map<std::string, std::unique_ptr<NodeContext>> nodes_;
    std::vector<std::shared_ptr<IEdgeContext>> edges_;
    std::shared_ptr<IScheduler> scheduler_;
    bool running_{false};
};

}
