#pragma once

#include <cstddef>

namespace runtime {

enum class BackpressurePolicy {
    Block,
    DropNewest,
    DropOldest,
    Unbounded
};

enum class MailboxPushResult {
    Accepted,
    DroppedNewest,
    DroppedOldest,
    Closed
};

enum class MailBoxKind {
    SPSC,
    MPMC
};

template <typename T>
class IMailBox {
public:
    virtual ~IMailBox() = default;
    virtual MailboxPushResult Push(T item, BackpressurePolicy policy) = 0;
    virtual bool TryPop(T& item) = 0;
    virtual bool WaitPop(T& item) = 0;
    virtual void Close() = 0;
    virtual void Open() = 0;
    virtual void Clear() = 0;
    virtual bool Empty() const = 0;
    virtual std::size_t Size() const = 0;
    virtual std::size_t Capacity() const = 0;
    virtual bool IsClosed() const = 0;
};

}
