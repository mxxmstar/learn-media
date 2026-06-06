#pragma once

#include <boost/lockfree/queue.hpp>
#include <atomic>
#include <cstddef>

namespace common {

template<typename T>
class BoundedMpscQueue {
public:
    explicit BoundedMpscQueue(size_t capacity)
        : queue_(capacity), size_(0) {}

    bool push(const T& item) {
        bool result = queue_.push(item);
        if (result) size_.fetch_add(1, std::memory_order_release);
        return result;
    }

    bool pop(T& item) {
        bool result = queue_.pop(item);
        if (result) size_.fetch_sub(1, std::memory_order_release);
        return result;
    }

    bool empty() const {
        return queue_.empty();
    }

    size_t size() const {
        return size_.load(std::memory_order_acquire);
    }

    void clear() {
        T item;
        size_t popped = 0;
        while (queue_.pop(item)) { ++popped; }
        if (popped > 0) size_.fetch_sub(popped, std::memory_order_release);
    }

private:
    boost::lockfree::queue<T> queue_;
    std::atomic<size_t> size_;
};

template<typename T>
class UnboundedMpscQueue {
public:
    explicit UnboundedMpscQueue(size_t initial_capacity = 64)
        : queue_(initial_capacity), size_(0) {}

    bool push(const T& item) {
        queue_.push(item);
        size_.fetch_add(1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        bool result = queue_.pop(item);
        if (result) size_.fetch_sub(1, std::memory_order_release);
        return result;
    }

    bool empty() const {
        return queue_.empty();
    }

    size_t size() const {
        return size_.load(std::memory_order_acquire);
    }

    void clear() {
        T item;
        size_t popped = 0;
        while (queue_.pop(item)) { ++popped; }
        if (popped > 0) size_.fetch_sub(popped, std::memory_order_release);
    }

private:
    boost::lockfree::queue<T, boost::lockfree::fixed_sized<false>> queue_;
    std::atomic<size_t> size_;
};

} // namespace common
