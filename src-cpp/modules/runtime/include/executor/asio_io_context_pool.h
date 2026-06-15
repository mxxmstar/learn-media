#pragma once

/**
 * @file asio_io_context_pool.h
 * @brief 基于 Boost.Asio 的 IO 上下文线程池及其管理器
 *
 * 提供多组独立的 io_context 线程池。每种类型的任务（通用计算、推理、IO）
 * 使用不同的线程池，避免推理任务阻塞 IO 线程。
 *
 * 架构：
 *   AsioIOContextPool（具体的线程池）
 *     - 持有 N 个 io_context 实例，每个在一个线程上 run()
 *     - 使用 work_guard 保持 io_context::run 不退出
 *     - Go 风格：通过轮询（fetch_add % size）选取 io_context
 *
 *   AsioIOContextPoolManager（线程池管理器）
 *     - 全局的命名池注册表
 *     - 管理 "general", "cpu", "inference", "io" 等命名池
 *     - 单例式管理
 */

#include <boost/asio.hpp>

#include <atomic>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace runtime {

/**
 * @brief io_context 线程池
 *
 * 每个线程运行一个独立的 io_context::run()。
 * 使用轮询策略（Round-Robin）分配 io_context。
 *
 * 设计要点：
 *   1. 每个 io_context 有独立的 work_guard，确保 run() 不会因无任务退出
 *   2. 析构函数自动调用 Stop() 等待线程退出
 *   3. 线程总数 = pool_size（通常 = CPU 核数）
 */
class AsioIOContextPool {
public:
    using IOContext = boost::asio::io_context;
    using WorkGuard = boost::asio::executor_work_guard<IOContext::executor_type>;

    /**
     * @brief 构造函数
     * @param pool_size 线程池大小（0 表示使用硬件并发数）
     *
     * 构造时即启动线程。每个线程运行一个 io_context。
     */
    explicit AsioIOContextPool(std::size_t pool_size = std::thread::hardware_concurrency())
        : next_io_context_index_(0), is_running_(true) {
        if (pool_size == 0) pool_size = 1;

        work_guards_.reserve(pool_size);

        // 创建 io_context 和 work_guard
        for (std::size_t i = 0; i < pool_size; ++i) {
            pool_.push_back(std::make_unique<IOContext>());
            work_guards_.emplace_back(boost::asio::make_work_guard(*pool_.back()));
        }

        // 创建线程（每个线程运行一个 io_context）
        threads_.reserve(pool_size);
        for (std::size_t i = 0; i < pool_size; ++i) {
            threads_.emplace_back([this, i]() {
                try {
                    pool_[i]->run();
                } catch (const std::exception& e) {
                    // io_context::run 一般不会抛出，除非 handler 抛异常
                    // 这里 catch 确保线程不会因异常而终止
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

    /**
     * @brief 获取一个 io_context 实例（轮询分配）
     *
     * 使用 fetch_add 原子递增 + 取模，
     * 确保多个线程同时获取不会冲突。
     */
    IOContext& GetIOContext() {
        if (!is_running_.load()) {
            throw std::runtime_error("AsioIOContextPool has been stopped");
        }
        auto index = next_io_context_index_.fetch_add(1) % pool_.size();
        return *pool_[index];
    }

    /**
     * @brief 停止线程池
     *
     * 停止过程：
     *   1. 清空 work_guards_（io_context 可以 stop）
     *   2. 向每个 io_context post 一个空任务（确保 run() 被唤醒）
     *   3. 等待所有线程 join
     */
    void Stop() {
        if (!is_running_.exchange(false)) {
            return;
        }

        // 释放 work_guard，允许 io_context::run 退出
        work_guards_.clear();

        // Post 空任务唤醒线程（确保它们检查到 stop 条件）
        for (auto& io_ctx : pool_) {
            boost::asio::post(*io_ctx, []() {});
        }

        // 等待所有线程退出
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
    std::vector<std::unique_ptr<IOContext>> pool_;      ///< io_context 列表
    std::vector<WorkGuard> work_guards_;                 ///< work 守卫（防止 run 退出）
    std::vector<std::thread> threads_;                   ///< 工作线程
    std::atomic<std::size_t> next_io_context_index_{0};  ///< 轮询索引
    std::atomic<bool> is_running_{false};                ///< 运行状态
};

/**
 * @brief 命名 io_context 线程池管理器（全局单例注册表）
 *
 * 管理多个命名的线程池。典型的池名称：
 *   - "general":    通用计算（编解码、缩放）
 *   - "cpu":        CPU 绑定计算
 *   - "inference":  AI 推理（GPU 不阻塞 CPU）
 *   - "io":         网络 IO、文件 IO
 *
 * 使用方式：
 *   // 初始化
 *   AsioIOContextPoolManager::Initialize("general", 4);
 *   AsioIOContextPoolManager::Initialize("inference", 2);
 *
 *   // 使用
 *   auto& pool = AsioIOContextPoolManager::GetInstance("general");
 *   boost::asio::post(pool.GetIOContext(), task);
 *
 *   // 程序退出时清理
 *   AsioIOContextPoolManager::StopAll();
 */
class AsioIOContextPoolManager {
public:
    /// @brief 初始化指定名称的线程池，如果已存在则忽略
    static void Initialize(const std::string& name, std::size_t pool_size = 0) {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        auto it = pools_.find(name);
        if (it != pools_.end()) {
            return;  // 已存在，不重复创建
        }

        if (pool_size == 0) {
            pool_size = std::thread::hardware_concurrency();
        }

        pools_[name] = std::make_unique<AsioIOContextPool>(pool_size);
    }

    /// @brief 获取指定名称的线程池
    static AsioIOContextPool& GetInstance(const std::string& name) {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        auto it = pools_.find(name);
        if (it == pools_.end()) {
            throw std::runtime_error("AsioIOContextPool '" + name + "' not initialized. Call Initialize() first.");
        }
        return *it->second;
    }

    /// @brief 停止并销毁指定名称的线程池
    static void Stop(const std::string& name) {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        auto it = pools_.find(name);
        if (it != pools_.end()) {
            it->second->Stop();
            pools_.erase(it);
        }
    }

    /// @brief 停止并销毁所有线程池
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
