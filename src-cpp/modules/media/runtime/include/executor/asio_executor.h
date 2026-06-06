#pragma once

/// @file asio_executor.h
/// @brief 基于 Boost.Asio 的生产级执行器
///
/// 使用全局 io_context 线程池（AsioIOContextPool）。
/// 支持多个命名线程池，避免推理任务阻塞其他任务。
/// 派生类：SingleThreadAsioExecutor / CpuAsioExecutor /
///         InferenceAsioExecutor / IOAsioExecutor

#include <boost/asio/post.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

#include "i_executor.h"
#include "asio_io_context_pool.h"

namespace runtime {

class AsioExecutor : public IExecutor {
public:
    using Task = std::function<void()>;
    using IOContext = boost::asio::io_context;

    explicit AsioExecutor(std::string name, std::string pool_name, std::size_t pool_size)
        : name_(std::move(name))
        , pool_name_(std::move(pool_name)) {
        initializePool(pool_name_, pool_size);
    }

    ~AsioExecutor() { Stop(); }

    AsioExecutor(const AsioExecutor&) = delete;
    AsioExecutor& operator=(const AsioExecutor&) = delete;

    bool Start() override {
        if (running_.exchange(true)) {
            return false;
        }
        accepting_.store(true);
        return true;
    }

    void Stop() override {
        if (!running_.exchange(false)) {
            return;
        }
        accepting_.store(false);
    }

    /// @brief 停止所有线程池（程序退出时调用）
    static void StopAllPools() {
        AsioIOContextPoolManager::StopAll();
    }

    bool Post(Task task) {
        if (!accepting_.load()) {
            return false;
        }
        pending_.fetch_add(1);

        try {
            auto& pool = AsioIOContextPoolManager::GetInstance(pool_name_);
            auto& io_ctx = pool.GetIOContext();
            boost::asio::post(io_ctx, [this, task = std::move(task)]() mutable {
                try {
                    task();
                } catch (...) {
                }
                pending_.fetch_sub(1);
            });
        } catch (...) {
            pending_.fetch_sub(1);
            return false;
        }
        return true;
    }

    IOContext& GetIOContext() {
        return AsioIOContextPoolManager::GetInstance(pool_name_).GetIOContext();
    }

    const std::string& Name() const {
        return name_;
    }

    const std::string& PoolName() const {
        return pool_name_;
    }

    std::size_t Pending() const {
        return pending_.load();
    }

private:
    static void initializePool(const std::string& pool_name, std::size_t pool_size) {
        if (!AsioIOContextPoolManager::Exists(pool_name)) {
            if (pool_size == 0) {
                pool_size = 1;
            }
            AsioIOContextPoolManager::Initialize(pool_name, pool_size);
        }
    }

    std::string name_;
    std::string pool_name_;
    std::atomic_bool accepting_{false};
    std::atomic<std::size_t> pending_{0};
    std::atomic<bool> running_{false};
};

class SingleThreadAsioExecutor : public AsioExecutor {
public:
    explicit SingleThreadAsioExecutor(std::string name = "single", std::string pool_name = "general", std::size_t pool_size = 0)
        : AsioExecutor(std::move(name), std::move(pool_name), pool_size) {}
};

class CpuAsioExecutor : public AsioExecutor {
public:
    explicit CpuAsioExecutor(std::string name = "cpu", std::string pool_name = "cpu", std::size_t pool_size = 0)
        : AsioExecutor(std::move(name), std::move(pool_name), pool_size) {}
};

class InferenceAsioExecutor : public AsioExecutor {
public:
    explicit InferenceAsioExecutor(std::string name = "inference", std::string pool_name = "inference", std::size_t pool_size = 0)
        : AsioExecutor(std::move(name), std::move(pool_name), pool_size) {}
};

class IOAsioExecutor : public AsioExecutor {
public:
    explicit IOAsioExecutor(std::string name = "io", std::string pool_name = "io", std::size_t pool_size = 0)
        : AsioExecutor(std::move(name), std::move(pool_name), pool_size) {}
};

} // namespace runtime