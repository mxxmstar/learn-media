#pragma once

#include "transport/i_mailbox.h"

#include <cstddef>
#include <optional>

namespace runtime {

template<typename T>
class ITransport {
public:
    virtual ~ITransport() = default;

    virtual MailboxPushResult Send(T data) = 0;
    virtual std::optional<T> TryReceive() = 0;
    virtual std::optional<T> Receive() = 0;
    virtual void Close() = 0;
    virtual bool Empty() const = 0;
    virtual std::size_t Size() const = 0;
};

}
