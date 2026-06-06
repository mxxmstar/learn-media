#pragma once

#include <memory>
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

    void Send(T data) {
        for (auto& transport : transports_) {
            transport->Send(data);
        }
    }

private:
    std::vector<std::shared_ptr<ITransport<T>>> transports_;
};

}
