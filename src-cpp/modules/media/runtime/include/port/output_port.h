#pragma once

#include "transport/i_transport.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace runtime {

template<typename T>
class ITransport;

template<typename T>
class OutputPort {
public:
    using Type = T;

    void AddTransport(std::shared_ptr<ITransport<T>> transport) {
        transports_.push_back(std::move(transport));
    }

    bool Send(T data) {
        if (transports_.empty()) {
            return true;
        }

        if (transports_.size() == 1) {
            return IsAccepted(transports_.front()->Send(std::move(data)));
        }

        if constexpr (std::is_copy_constructible_v<T>) {
            bool accepted = true;
            for (auto& transport : transports_) {
                accepted = IsAccepted(transport->Send(data)) && accepted;
            }
            return accepted;
        } else {
            return false;
        }
    }

private:
    static bool IsAccepted(MailboxPushResult result) {
        return result == MailboxPushResult::Accepted ||
               result == MailboxPushResult::DroppedOldest;
    }

    std::vector<std::shared_ptr<ITransport<T>>> transports_;
};

}
