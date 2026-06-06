#pragma once

#include <boost/pool/pool.hpp>
#include <cstring>
#include <mutex>

template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t initial_chunks = 64)
        : pool_(sizeof(T), initial_chunks) {}

    ~ObjectPool() = default;
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

    T* Allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        void* mem = pool_.malloc();
        if (!mem) return nullptr;
        std::memset(mem, 0, sizeof(T));
        return static_cast<T*>(mem);
    }

    void Deallocate(T* obj) {
        if (!obj) return;
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.free(obj);
    }

    template <typename... Args>
    T* Acquire(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        void* mem = pool_.malloc();
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    void Release(T* obj) {
        if (!obj) return;
        obj->~T();
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.free(obj);
    }

private:
    boost::pool<> pool_;
    std::mutex    mutex_;
};
