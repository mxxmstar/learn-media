#pragma once

#include "i_transport.h"
#include "spsc_mailbox.h"

#include <functional>

namespace runtime {

template<typename T>
class QueueTransport : public ITransport<T> {
public:
    using NotifyCallback = std::function<void()>;

    explicit QueueTransport(std::size_t capacity = 64,
                            BackpressurePolicy policy = BackpressurePolicy::DropOldest)
        : mailbox_(capacity), policy_(policy) {}

    void SetNotifyCallback(NotifyCallback cb) {
        notify_ = std::move(cb);
    }

    bool Send(T data) override {
        auto result = mailbox_.Push(std::move(data), policy_);
        if (result != MailboxPushResult::Closed && notify_) {
            notify_();
        }
        return result != MailboxPushResult::Closed;
    }

    bool Receive(T& data) override {
        return mailbox_.WaitPop(data);
    }

    bool Empty() const {
        return mailbox_.Empty();
    }

    std::size_t Size() const {
        return mailbox_.Size();
    }

private:
    SPSCMailBox<T> mailbox_;
    BackpressurePolicy policy_;
    NotifyCallback notify_;
};

}
