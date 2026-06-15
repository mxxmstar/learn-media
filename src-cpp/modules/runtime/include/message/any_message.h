#pragma once

#include <any>
#include <typeinfo>
#include <utility>

namespace runtime {

using MessageVariant = std::any;

class AnyMessage {
public:
    AnyMessage() = default;

    template <typename T>
    AnyMessage(T&& data) : data_(std::forward<T>(data)) {
    }

    bool Empty() const {
        return !data_.has_value();
    }

    const std::type_info& Type() const {
        return data_.type();
    }

    template <typename T>
    bool Is() const {
        return std::any_cast<T>(&data_) != nullptr;
    }

    template <typename T>
    T& Get() {
        return std::any_cast<T&>(data_);
    }

    template <typename T>
    const T& Get() const {
        return std::any_cast<const T&>(data_);
    }

    template <typename T>
    T* TryGet() {
        return std::any_cast<T>(&data_);
    }

    template <typename T>
    const T* TryGet() const {
        return std::any_cast<T>(&data_);
    }

private:
    std::any data_;
};

}  // namespace runtime
