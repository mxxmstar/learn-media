#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace common::thread {

class IExecutor {
public:
    using Task = std::function<void()>;

    virtual ~IExecutor() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual bool Post(Task task) = 0;
    virtual const std::string& Name() const = 0;
    virtual std::size_t Pending() const = 0;
};

class ThreadPoolExecutor : public IExecutor {
public:
    explicit ThreadPoolExecutor(std::string name, std::size_t thread_count = 1)
        : name_(std::move(name))
        , thread_count_(thread_count == 0 ? 1 : thread_count) {}

    ~ThreadPoolExecutor() override {
        Stop();
    }

    ThreadPoolExecutor(const ThreadPoolExecutor&) = delete;
    ThreadPoolExecutor& operator=(const ThreadPoolExecutor&) = delete;

    void Start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return;
        }

        stopping_ = false;
        running_ = true;
        workers_.reserve(thread_count_);
        for (std::size_t i = 0; i < thread_count_; ++i) {
            workers_.emplace_back([this]() { WorkerLoop(); });
        }
    }

    void Stop() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }
            stopping_ = true;
        }

        cv_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        workers_.clear();

        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }

    bool Post(Task task) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ || stopping_) {
                return false;
            }
            tasks_.push_back(std::move(task));
        }

        cv_.notify_one();
        return true;
    }

    const std::string& Name() const override {
        return name_;
    }

    std::size_t Pending() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size() + active_tasks_.load();
    }

protected:
    void WorkerLoop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() {
                    return stopping_ || !tasks_.empty();
                });

                if (stopping_ && tasks_.empty()) {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop_front();
                active_tasks_.fetch_add(1);
            }

            try {
                task();
            } catch (...) {
            }

            active_tasks_.fetch_sub(1);
        }
    }

private:
    std::string name_;
    std::size_t thread_count_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    std::vector<std::thread> workers_;
    std::atomic<std::size_t> active_tasks_{0};
    bool running_{false};
    bool stopping_{false};
};

class SingleThreadExecutor : public ThreadPoolExecutor {
public:
    explicit SingleThreadExecutor(std::string name = "single")
        : ThreadPoolExecutor(std::move(name), 1) {}
};

class InferenceExecutor : public ThreadPoolExecutor {
public:
    explicit InferenceExecutor(std::string name = "inference", std::size_t thread_count = 1)
        : ThreadPoolExecutor(std::move(name), thread_count) {}
};

class IOExecutor : public ThreadPoolExecutor {
public:
    explicit IOExecutor(std::string name = "io", std::size_t thread_count = 1)
        : ThreadPoolExecutor(std::move(name), thread_count) {}
};

} // namespace common::thread
