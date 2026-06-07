#pragma once

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <string>

#include "graph/node_context.h"
#include "scheduler/i_scheduler.h"
#include "transport/i_transport.h"

namespace runtime {

class IEdgeContext : public std::enable_shared_from_this<IEdgeContext> {
public:
    virtual ~IEdgeContext() = default;
    virtual void ExecuteDrain() = 0;
    virtual void Close() = 0;
    virtual bool Empty() const = 0;
    virtual NodeContext* GetDestination() const = 0;

    bool TrySchedule() {
        bool expected = false;
        if (!scheduled.compare_exchange_strong(expected, true)) {
            return true;
        }

        auto scheduler = scheduler_.lock();
        if (!scheduler || !scheduler->Notify(shared_from_this())) {
            scheduled.store(false);
            if (auto metrics = dst_metrics_.lock()) {
                metrics->rejected.fetch_add(1);
            }
            return false;
        }

        return true;
    }

    std::atomic<bool> scheduled{false};
    NodeContext* dst_{nullptr};
    std::weak_ptr<NodeMetrics> dst_metrics_;
    std::weak_ptr<IScheduler> scheduler_;
    std::string edge_id;
    std::string src_id;
};

template<typename T>
using Consumer = std::function<void(T)>;

template<typename T>
class EdgeContext : public IEdgeContext {
public:
    std::shared_ptr<ITransport<T>> transport;
    Consumer<T> consumer_;

    NodeContext* GetDestination() const override {
        return dst_;
    }

    void Close() override {
        if (transport) {
            transport->Close();
        }
    }

    bool Empty() const override {
        return !transport || transport->Empty();
    }

    void ExecuteDrain() override {
        struct ScheduleReset {
            EdgeContext* edge;
            ~ScheduleReset() {
                edge->scheduled.store(false);
                if (edge->transport && !edge->transport->Empty()) {
                    edge->TrySchedule();
                }
            }
        } reset{this};

        while (transport) {
            auto msg = transport->TryReceive();
            if (!msg.has_value()) {
                break;
            }

            auto* dst = GetDestination();
            if (!dst) {
                if (auto metrics = dst_metrics_.lock()) {
                    metrics->rejected.fetch_add(1);
                }
                continue;
            }

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
