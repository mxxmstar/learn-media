#pragma once

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

enum class NodeExecutionMode {
    Serialized,
    Concurrent
};

struct NodeOptions {
    NodeExecutionMode execution_mode{NodeExecutionMode::Serialized};
};

struct NodeContext {
    using Task = IExecutor::Task;

    std::string id;
    std::shared_ptr<INode> node;
    std::shared_ptr<IExecutor> executor;
    std::shared_ptr<NodeMetrics> metrics;
    NodeExecutionMode execution_mode{NodeExecutionMode::Serialized};

    std::unordered_map<std::type_index, std::any> input_ports;
    std::unordered_map<std::type_index, std::any> output_ports;

    bool Dispatch(Task task) {
        if (execution_mode == NodeExecutionMode::Concurrent) {
            if (!executor || !executor->Post(std::move(task))) {
                CountRejected();
                return false;
            }
            return true;
        }

        bool should_schedule = false;
        {
            std::lock_guard<std::mutex> lock(dispatch_mutex_);
            if (dispatch_closed_) {
                CountRejected();
                return false;
            }

            dispatch_queue_.push_back(std::move(task));
            if (!dispatch_active_) {
                dispatch_active_ = true;
                should_schedule = true;
            }
        }

        if (!should_schedule) {
            return true;
        }

        return ScheduleDispatch();
    }

    void CloseDispatch() {
        std::lock_guard<std::mutex> lock(dispatch_mutex_);
        dispatch_closed_ = true;
        dispatch_queue_.clear();
    }

    void OpenDispatch() {
        std::lock_guard<std::mutex> lock(dispatch_mutex_);
        dispatch_closed_ = false;
        dispatch_active_ = false;
        dispatch_queue_.clear();
    }

private:
    bool ScheduleDispatch() {
        if (!executor || !executor->Post([this]() { RunDispatchOne(); })) {
            std::lock_guard<std::mutex> lock(dispatch_mutex_);
            dispatch_active_ = false;
            CountRejected();
            return false;
        }

        return true;
    }

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

    std::mutex dispatch_mutex_;
    std::deque<Task> dispatch_queue_;
    bool dispatch_active_{false};
    bool dispatch_closed_{false};
};

}
