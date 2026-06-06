#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <any>
#include <typeindex>
#include <unordered_map>

namespace runtime {

class INode;
class IExecutor;

struct NodeMetricsSnapshot {
    std::uint64_t enqueued{0};
    std::uint64_t processed{0};
    std::uint64_t dropped{0};
    std::uint64_t rejected{0};
    std::uint64_t errors{0};
};

struct NodeMetrics {
    std::atomic<std::uint64_t> enqueued{0};
    std::atomic<std::uint64_t> processed{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> errors{0};

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

struct NodeContext {
    std::string id;
    std::shared_ptr<INode> node;
    std::shared_ptr<IExecutor> executor;
    std::shared_ptr<NodeMetrics> metrics;

    // type-erased port access for Connect<T>
    std::unordered_map<std::type_index, std::any> input_ports;
    std::unordered_map<std::type_index, std::any> output_ports;
};

}
