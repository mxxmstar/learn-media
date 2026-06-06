#pragma once

#include <functional>
#include <memory>

namespace runtime {

template<typename T>
class ITransport;

template<typename T>
class InputPort {
public:
    using Type = T;
    using Handler = std::function<void(T)>;

    void Bind(std::shared_ptr<ITransport<T>> transport) {
        transport_ = std::move(transport);
    }

    void SetHandler(Handler handler) {
        handler_ = std::move(handler);
    }

    void Receive(T data) {
        if (handler_) {
            handler_(std::move(data));
        }
    }

private:
    std::shared_ptr<ITransport<T>> transport_;
    Handler handler_;
};

}
