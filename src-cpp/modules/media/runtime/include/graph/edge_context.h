#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "transport/queue_transport.h"

namespace runtime {

struct NodeContext;
class IScheduler;

class IEdgeContext {
public:
    virtual ~IEdgeContext() = default;
    virtual void ExecuteDrain() = 0;
    virtual NodeContext* GetDestination() const = 0;

    std::atomic<bool> scheduled{false};
    NodeContext* dst_{nullptr};
    IScheduler* scheduler_{nullptr};
    std::string edge_id;
    std::string src_id;
};

template<typename T>
using Consumer = std::function<void(T)>;

template<typename T>
class EdgeContext : public IEdgeContext {
public:
    std::shared_ptr<QueueTransport<T>> transport;
    Consumer<T> consumer_;

    NodeContext* GetDestination() const override {
        return dst_;
    }

    void ExecuteDrain() override {
        T msg;
        while (transport->Receive(msg)) {
            if (consumer_) {
                consumer_(std::move(msg));
            }
        }
        scheduled.store(false);
        if (!transport->Empty()) {
            bool expected = false;
            if (scheduled.compare_exchange_strong(expected, true)) {
                if (scheduler_) {
                    scheduler_->Notify(this);
                }
            }
        }
    }
};

}
