#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace common::thread {

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

template <typename T>
class Mailbox {
public:
    explicit Mailbox(std::size_t capacity = 64)
        : capacity_(capacity) {}

    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;

    MailboxPushResult Push(T item, BackpressurePolicy policy) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (closed_) {
            return MailboxPushResult::Closed;
        }

        if (policy == BackpressurePolicy::Unbounded || capacity_ == 0) {
            queue_.push_back(std::move(item));
            cv_.notify_one();
            return MailboxPushResult::Accepted;
        }

        if (policy == BackpressurePolicy::Block) {
            cv_.wait(lock, [this]() {
                return closed_ || queue_.size() < capacity_;
            });

            if (closed_) {
                return MailboxPushResult::Closed;
            }

            queue_.push_back(std::move(item));
            cv_.notify_one();
            return MailboxPushResult::Accepted;
        }

        if (queue_.size() < capacity_) {
            queue_.push_back(std::move(item));
            cv_.notify_one();
            return MailboxPushResult::Accepted;
        }

        if (policy == BackpressurePolicy::DropOldest) {
            queue_.pop_front();
            queue_.push_back(std::move(item));
            cv_.notify_one();
            return MailboxPushResult::DroppedOldest;
        }

        return MailboxPushResult::DroppedNewest;
    }

    bool TryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop_front();
        cv_.notify_one();
        return true;
    }

    bool WaitPop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop_front();
        cv_.notify_one();
        return true;
    }

    void Close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    void Open() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = false;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        cv_.notify_all();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    std::size_t Capacity() const {
        return capacity_;
    }

    bool IsClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    bool closed_{false};
};

} // namespace common::thread
