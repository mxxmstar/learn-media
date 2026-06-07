#pragma once
#include <boost/asio.hpp>
#include <vector>
#include <thread>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <exception>
#include <atomic>

namespace runtime {

/// @brief io_context线程池,启用多个线程，每个线程运行一个io_context实例
class AsioIOContextPool {
public:
    using IOContext = boost::asio::io_context;
    using WorkGuard = boost::asio::executor_work_guard<IOContext::executor_type>;

    explicit AsioIOContextPool(std::size_t pool_size = std::thread::hardware_concurrency())
        : next_io_context_index_(0), is_running_(true) {
        if (pool_size == 0) pool_size = 1;

        work_guards_.reserve(pool_size);

        for (std::size_t i = 0; i < pool_size; ++i) {
            pool_.push_back(std::make_unique<IOContext>());
            work_guards_.emplace_back(boost::asio::make_work_guard(*pool_.back()));
        }

        threads_.reserve(pool_size);
        for (std::size_t i = 0; i < pool_size; ++i) {
            threads_.emplace_back([this, i]() {
                try {
                    pool_[i]->run();
                } catch (const std::exception& e) {
                } catch(...) {
                }
            });
        }
    }

    ~AsioIOContextPool() {
        Stop();
    }

    AsioIOContextPool(const AsioIOContextPool&) = delete;
    AsioIOContextPool& operator=(const AsioIOContextPool&) = delete;

    /// @brief 获取一个io_context实例（轮询分配）
    IOContext& GetIOContext() {
        if (!is_running_.load()) {
            throw std::runtime_error("AsioIOContextPool has been stopped");
        }
        auto index = next_io_context_index_.fetch_add(1) % pool_.size();
        return *pool_[index];
    }

    /// @brief 停止线程池
    void Stop() {
        if (!is_running_.exchange(false)) {
            return;
        }

        work_guards_.clear();

        for (auto& io_ctx : pool_) {
            boost::asio::post(*io_ctx, []() {});
        }

        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        threads_.clear();
    }

    bool IsRunning() const {
        return is_running_.load();
    }

    std::size_t Size() const {
        return pool_.size();
    }

private:
    std::vector<std::unique_ptr<IOContext>> pool_;
    std::vector<WorkGuard> work_guards_;
    std::vector<std::thread> threads_;
    std::atomic<std::size_t> next_io_context_index_{0};
    std::atomic<bool> is_running_{false};
};

/// @brief io_context线程池管理器，管理多个命名线程池
class AsioIOContextPoolManager {
public:
    /// @brief 初始化指定名称的线程池
    /// @param name 线程池名称（如"general", "inference"）
    /// @param pool_size 线程池大小（0表示使用硬件并发数）
    static void Initialize(const std::string& name, std::size_t pool_size = 0) {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        auto it = pools_.find(name);
        if (it != pools_.end()) {
            return;
        }

        if (pool_size == 0) {
            pool_size = std::thread::hardware_concurrency();
        }

        pools_[name] = std::make_unique<AsioIOContextPool>(pool_size);
    }

    /// @brief 获取指定名称的线程池
    /// @param name 线程池名称
    /// @return 线程池引用
    static AsioIOContextPool& GetInstance(const std::string& name) {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        auto it = pools_.find(name);
        if (it == pools_.end()) {
            throw std::runtime_error("AsioIOContextPool '" + name + "' not initialized. Call Initialize() first.");
        }
        return *it->second;
    }

    /// @brief 停止指定名称的线程池
    static void Stop(const std::string& name) {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        auto it = pools_.find(name);
        if (it != pools_.end()) {
            it->second->Stop();
            pools_.erase(it);
        }
    }

    /// @brief 停止所有线程池
    static void StopAll() {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        for (auto& pair : pools_) {
            pair.second->Stop();
        }
        pools_.clear();
    }

    /// @brief 检查线程池是否存在
    static bool Exists(const std::string& name) {
        std::lock_guard<std::mutex> lock(pools_mutex_);
        return pools_.find(name) != pools_.end();
    }

private:
    static std::map<std::string, std::unique_ptr<AsioIOContextPool>> pools_;
    static std::mutex pools_mutex_;
};

inline std::map<std::string, std::unique_ptr<AsioIOContextPool>> AsioIOContextPoolManager::pools_;
inline std::mutex AsioIOContextPoolManager::pools_mutex_;    
    

}