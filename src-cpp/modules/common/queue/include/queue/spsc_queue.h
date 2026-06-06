#pragma once

#include <boost/lockfree/spsc_queue.hpp>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstddef>

namespace common {

template<typename T>
class BoundedSpscQueue {
public:
    explicit BoundedSpscQueue(size_t capacity)
        : capacity_(capacity)
        , queue_(capacity) {}

    bool push(const T& item) {
        return queue_.push(item);
    }

    bool push(T&& item) {
        return queue_.push(std::move(item));
    }

    bool pop(T& item) {
        return queue_.pop(item);
    }

    bool empty() const {
        return const_cast<boost::lockfree::spsc_queue<T>&>(queue_).empty();
    }

    bool full() const {
        return const_cast<boost::lockfree::spsc_queue<T>&>(queue_).full();
    }

    size_t size() const {
        return queue_.read_available();
    }

    size_t available() const {
        return queue_.write_available();
    }

    size_t capacity() const {
        return capacity_;
    }

    void clear() {
        T item;
        while (queue_.pop(item)) {}
    }

private:
    size_t capacity_;
    boost::lockfree::spsc_queue<T> queue_;
};

template<typename T>
class UnboundedSpscQueue {
public:
    UnboundedSpscQueue() = default;

    void push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(item);
        }
        cv_.notify_one();
    }

    void push(T&& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return !queue_.empty() || closed_; });
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool pop_for(T& item, const std::chrono::milliseconds& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this]() { return !queue_.empty() || closed_; })) {
            return false;
        }
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        closed_ = false;
    }

private:
    std::deque<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_{false};
};

} // namespace common
