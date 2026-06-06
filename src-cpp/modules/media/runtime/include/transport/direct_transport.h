#pragma once

#include "i_transport.h"
#include <optional>

namespace runtime {

template<typename T>
class DirectTransport : public ITransport<T> {
public:
    bool Send(T data) override {
        buffer_ = std::move(data);
        return true;
    }

    bool Receive(T& data) override {
        if (!buffer_.has_value()) return false;
        data = std::move(*buffer_);
        buffer_.reset();
        return true;
    }

private:
    std::optional<T> buffer_;
};

}
