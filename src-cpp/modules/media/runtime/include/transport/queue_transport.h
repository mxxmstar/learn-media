#pragma once

#include "i_transport.h"
#include "spsc_mailbox.h"

#include <cstddef>
#include <functional>
#include <optional>

namespace runtime {

template<typename T>
class QueueTransport : public ITransport<T> {
public:
    using NotifyCallback = std::function<void()>;
    using SendResultCallback = std::function<void(MailboxPushResult)>;

    explicit QueueTransport(std::size_t capacity = 64,
                            BackpressurePolicy policy = BackpressurePolicy::DropOldest)
        : mailbox_(policy == BackpressurePolicy::Unbounded ? 0 : capacity)
        , policy_(policy) {}

    void SetNotifyCallback(NotifyCallback cb) {
        notify_ = std::move(cb);
    }

    void SetSendResultCallback(SendResultCallback cb) {
        on_send_result_ = std::move(cb);
    }

    MailboxPushResult Send(T data) override {
        auto result = mailbox_.Push(std::move(data), policy_);
        if (on_send_result_) {
            on_send_result_(result);
        }
        if ((result == MailboxPushResult::Accepted ||
             result == MailboxPushResult::DroppedOldest) && notify_) {
            notify_();
        }
        return result;
    }

    std::optional<T> TryReceive() override {
        return mailbox_.TryPopValue();
    }

    std::optional<T> Receive() override {
        return mailbox_.WaitPopValue();
    }

    void Close() override {
        mailbox_.Close();
    }

    bool Empty() const override {
        return mailbox_.Empty();
    }

    std::size_t Size() const override {
        return mailbox_.Size();
    }

private:
    SPSCMailBox<T> mailbox_;
    BackpressurePolicy policy_;
    NotifyCallback notify_;
    SendResultCallback on_send_result_;
};

}
