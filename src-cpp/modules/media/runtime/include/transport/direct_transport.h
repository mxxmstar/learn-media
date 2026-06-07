#pragma once

#include "i_transport.h"

#include <cstddef>
#include <functional>
#include <optional>

namespace runtime {

template<typename T>
class DirectTransport : public ITransport<T> {
public:
    using Consumer = std::function<void(T)>;
    using SendResultCallback = std::function<void(MailboxPushResult)>;

    void SetConsumer(Consumer consumer) {
        consumer_ = std::move(consumer);
    }

    void SetSendResultCallback(SendResultCallback cb) {
        on_send_result_ = std::move(cb);
    }

    MailboxPushResult Send(T data) override {
        if (closed_) {
            if (on_send_result_) {
                on_send_result_(MailboxPushResult::Closed);
            }
            return MailboxPushResult::Closed;
        }

        if (on_send_result_) {
            on_send_result_(MailboxPushResult::Accepted);
        }
        if (consumer_) {
            consumer_(std::move(data));
        }
        return MailboxPushResult::Accepted;
    }

    std::optional<T> TryReceive() override {
        return std::nullopt;
    }

    std::optional<T> Receive() override {
        return TryReceive();
    }

    void Close() override {
        closed_ = true;
    }

    bool Empty() const override {
        return true;
    }

    std::size_t Size() const override {
        return 0;
    }

private:
    Consumer consumer_;
    SendResultCallback on_send_result_;
    bool closed_{false};
};

}
