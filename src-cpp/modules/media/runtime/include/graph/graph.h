#pragma once

#include <any>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "graph/edge_context.h"
#include "graph/node_context.h"
#include "node/i_node.h"
#include "port/input_port.h"
#include "port/output_port.h"
#include "scheduler/i_scheduler.h"
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
        auto node = std::make_shared<T>(std::forward<Args>(args)...);

        NodeContext ctx;
        ctx.id = id;
        ctx.node = std::static_pointer_cast<INode>(node);
        ctx.executor = std::move(executor);
        ctx.metrics = std::make_shared<NodeMetrics>();

        if constexpr (detail::HasInputMethod<T>::value) {
            auto& input = node->Input();
            using InType = typename std::decay_t<decltype(input)>::Type;
            ctx.input_ports[std::type_index(typeid(InType))] = &input;
        }

        if constexpr (detail::HasOutputMethod<T>::value) {
            auto& output = node->Output();
            using OutType = typename std::decay_t<decltype(output)>::Type;
            ctx.output_ports[std::type_index(typeid(OutType))] = &output;
        }

        nodes_[id] = std::move(ctx);
        return id;
    }

    template<typename T>
    void Connect(const std::string& src, const std::string& dst,
                 TransportType type = TransportType::Queue,
                 std::size_t capacity = 64,
                 BackpressurePolicy policy = BackpressurePolicy::DropOldest) {
        auto src_it = nodes_.find(src);
        auto dst_it = nodes_.find(dst);
        if (src_it == nodes_.end() || dst_it == nodes_.end()) {
            return;
        }

        auto& src_ctx = src_it->second;
        auto& dst_ctx = dst_it->second;

        auto out_it = src_ctx.output_ports.find(std::type_index(typeid(T)));
        auto in_it = dst_ctx.input_ports.find(std::type_index(typeid(T)));
        if (out_it == src_ctx.output_ports.end() || in_it == dst_ctx.input_ports.end()) {
            return;
        }

        auto* output = std::any_cast<OutputPort<T>*>(out_it->second);
        auto* input = std::any_cast<InputPort<T>*>(in_it->second);

        auto edge = std::make_shared<EdgeContext<T>>();
        edge->edge_id = src + "->" + dst;
        edge->src_id = src;
        edge->dst_ = &dst_ctx;
        edge->scheduler_ = scheduler_.get();

        edge->transport = std::make_shared<QueueTransport<T>>(capacity, policy);

        edge->consumer_ = [input](T msg) {
            if (input) {
                input->Receive(std::move(msg));
            }
        };

        auto edge_raw = edge.get();
        edge->transport->SetNotifyCallback([edge_raw]() {
            bool expected = false;
            if (edge_raw->scheduled.compare_exchange_strong(expected, true)) {
                if (edge_raw->scheduler_) {
                    edge_raw->scheduler_->Notify(edge_raw);
                }
            }
        });

        output->AddTransport(edge->transport);

        edges_.push_back(std::move(edge));
    }

    void SetScheduler(std::shared_ptr<IScheduler> scheduler) {
        scheduler_ = std::move(scheduler);
    }

    NodeContext* GetNode(const std::string& id) {
        auto it = nodes_.find(id);
        return it != nodes_.end() ? &it->second : nullptr;
    }

    bool Start() {
        for (auto& [_, ctx] : nodes_) {
            if (ctx.node) {
                ctx.node->Init();
            }
        }
        for (auto& [_, ctx] : nodes_) {
            if (ctx.executor) {
                ctx.executor->Start();
            }
        }
        for (auto& [_, ctx] : nodes_) {
            if (ctx.node) {
                ctx.node->Start();
            }
        }
        return true;
    }

    void Stop() {
        for (auto& [_, ctx] : nodes_) {
            if (ctx.node) {
                ctx.node->Stop();
            }
        }
        for (auto& [_, ctx] : nodes_) {
            if (ctx.executor) {
                ctx.executor->Stop();
            }
        }
        for (auto& [_, ctx] : nodes_) {
            if (ctx.node) {
                ctx.node->Deinit();
            }
        }
    }

    bool GetMetrics(const std::string& id, NodeMetricsSnapshot& out) const {
        auto it = nodes_.find(id);
        if (it == nodes_.end() || !it->second.metrics) return false;
        out = it->second.metrics->Snapshot();
        return true;
    }

private:
    std::unordered_map<std::string, NodeContext> nodes_;
    std::vector<std::shared_ptr<IEdgeContext>> edges_;
    std::shared_ptr<IScheduler> scheduler_;
};

}
