#pragma once

/**
 * @file asio_executor.h
 * @brief 基于 Boost.Asio 的生产级执行器
 *
 * 框架默认的 Executor 实现。通过 AsioIOContextPool 将任务提交到
 * 指定的 io_context 线程池执行。
 *
 * 设计要点：
 *
 * 1. State + shared_ptr 防止悬垂引用
 *    AsioExecutor 把所有 mutable 状态（accepting, running, pending）
 *    放在 shared_ptr<State> 中。lambda 捕获 state 而非 this，
 *    这样即使 Executor 被析构，正在执行的任务仍然可以安全地
 *    更新 pending 计数并通知条件变量。
 *
 * 2. 停止等待（Stop 同步）
 *    Stop() 通过 atomic + cv 等待所有 pending 任务执行完毕。
 *    条件变量等待点：pending 下降到 0。
 *    确保 Stop 返回后，所有提交的任务都已执行完成。
 *
 * 3. 命名线程池
 *    不同类型的任务使用不同的线程池（见 AsioIOContextPoolManager）：
 *    - SingleThreadAsioExecutor: 单线程，用于序列化节点
 *    - CpuAsioExecutor: CPU 计算
 *    - InferenceAsioExecutor: 推理任务
 *    - IOAsioExecutor: 网络/文件 IO
 *
 * 4. 异常安全
 *    Post 中捕获所有异常（包括 boost::asio::post 抛出的异常），
 *    确保 pending 计数正确递减。
 */

#include <boost/asio/post.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "i_executor.h"
#include "asio_io_context_pool.h"

namespace runtime {

class AsioExecutor : public IExecutor {
public:
    using Task = std::function<void()>;
    using IOContext = boost::asio::io_context;

    /**
     * @brief 构造函数
     * @param name      执行器名称（仅用于标识）
     * @param pool_name 使用的线程池名称
     * @param pool_size 线程池大小（0=使用硬件并发数）
     *
     * 首次构造时会自动初始化线程池（如果尚未初始化）。
     */
    explicit AsioExecutor(std::string name, std::string pool_name, std::size_t pool_size = 0)
        : name_(std::move(name))
        , pool_name_(std::move(pool_name))
        , state_(std::make_shared<State>()) {
        initializePool(pool_name_, pool_size);
    }

    ~AsioExecutor() override {
        Stop();
    }

    AsioExecutor(const AsioExecutor&) = delete;
    AsioExecutor& operator=(const AsioExecutor&) = delete;

    /// @brief 启动执行器，开始接受任务
    bool Start() override {
        state_->accepting.store(true);
        state_->running.store(true);
        return true;
    }

    /**
     * @brief 停止执行器，等待所有任务完成
     *
     * 过程：
     *   1. 设置 running = false, accepting = false
     *   2. 持有 mutex，等待 pending == 0
     *   3. cv.wait 在 Post 完成时的 notify_all 上
     */
    void Stop() override {
        if (!state_->running.exchange(false)) {
            state_->accepting.store(false);
            return;  // 已经停止
        }

        state_->accepting.store(false);
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->cv.wait(lock, [state = state_]() {
            return state->pending.load() == 0;
        });
        // 注意：锁在 wait 返回后自动释放
    }

    /// @brief 停止所有全局线程池（程序退出时调用）
    static void StopAllPools() {
        AsioIOContextPoolManager::StopAll();
    }

    /**
     * @brief 提交任务到执行器
     *
     * 过程：
     *   1. 检查 accepting 标志（不接受已停止的 Executor）
     *   2. pending.fetch_add(1)
     *   3. 通过 boost::asio::post 提交到 io_context
     *   4. lambda 捕获 state shared_ptr，执行 task
     *   5. 执行完后 pending.fetch_sub(1)
     *   6. 如果减到 0（fetch_sub 返回 1），通知 cv
     *   7. 如果 Post 抛出异常，回退 pending 并通知
     */
    bool Post(Task task) override {
        if (!state_->accepting.load()) {
            return false;
        }

        state_->pending.fetch_add(1);

        try {
            auto& pool = AsioIOContextPoolManager::GetInstance(pool_name_);
            auto& io_ctx = pool.GetIOContext();
            auto state = state_;
            boost::asio::post(io_ctx, [state, task = std::move(task)]() mutable {
                try {
                    task();
                } catch (...) {
                    // 任务本身的异常被吞掉，不影响管线
                }

                // 最后一个任务完成时通知 Stop()
                if (state->pending.fetch_sub(1) == 1) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->cv.notify_all();
                }
            });
        } catch (...) {
            // Post 本身失败（如线程池已销毁），回退 pending 计数
            if (state_->pending.fetch_sub(1) == 1) {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->cv.notify_all();
            }
            return false;
        }

        return true;
    }

    /// @brief 获取关联的 io_context（可用于直接 post 任务）
    IOContext& GetIOContext() {
        return AsioIOContextPoolManager::GetInstance(pool_name_).GetIOContext();
    }

    const std::string& Name() const {
        return name_;
    }

    const std::string& PoolName() const {
        return pool_name_;
    }

    /// @brief 当前 pending 任务数
    std::size_t Pending() const {
        return state_->pending.load();
    }

private:
    /**
     * @brief Executor 的可变状态
     *
     * 通过 shared_ptr 管理，确保 lambda 中不会引用已析构的 this。
     * 所有成员都是线程安全的 atomic + mutex。
     */
    struct State {
        std::atomic_bool accepting{false};          ///< 是否接受新任务
        std::atomic_bool running{false};             ///< 运行状态
        std::atomic<std::size_t> pending{0};         ///< 正在执行或排队的任务数
        std::mutex mutex;                            ///< 与 cv 配合使用
        std::condition_variable cv;                  ///< 通知 Stop 线程
    };

    /// @brief 初始化线程池（如果尚未初始化）
    static void initializePool(const std::string& pool_name, std::size_t pool_size) {
        if (!AsioIOContextPoolManager::Exists(pool_name)) {
            if (pool_size == 0) {
                pool_size = 1;  // 不指定时默认单线程
                // 注：AsioIOContextPool 的构造函数也有同样的兜底逻辑
            }
            AsioIOContextPoolManager::Initialize(pool_name, pool_size);
        }
    }

    std::string name_;                  ///< 执行器名称
    std::string pool_name_;             ///< 线程池名称
    std::shared_ptr<State> state_;      ///< 共享状态
};

// —— 预定义的命名执行器 ——

/// @brief 单线程执行器
class SingleThreadAsioExecutor : public AsioExecutor {
public:
    explicit SingleThreadAsioExecutor(std::string name = "single", std::string pool_name = "general", std::size_t pool_size = 0)
        : AsioExecutor(std::move(name), std::move(pool_name), pool_size) {}
};

/// @brief CPU 计算执行器
class CpuAsioExecutor : public AsioExecutor {
public:
    explicit CpuAsioExecutor(std::string name = "cpu", std::string pool_name = "cpu", std::size_t pool_size = 0)
        : AsioExecutor(std::move(name), std::move(pool_name), pool_size) {}
};

/// @brief 推理执行器
class InferenceAsioExecutor : public AsioExecutor {
public:
    explicit InferenceAsioExecutor(std::string name = "inference", std::string pool_name = "inference", std::size_t pool_size = 0)
        : AsioExecutor(std::move(name), std::move(pool_name), pool_size) {}
};

/// @brief IO 执行器
class IOAsioExecutor : public AsioExecutor {
public:
    explicit IOAsioExecutor(std::string name = "io", std::string pool_name = "io", std::size_t pool_size = 0)
        : AsioExecutor(std::move(name), std::move(pool_name), pool_size) {}
};

}
