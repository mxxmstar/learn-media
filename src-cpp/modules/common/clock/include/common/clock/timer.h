#include <boost/asio.hpp>
#include <functional>
#include <chrono>
#include <memory>
#include <mutex>
#include <iostream>

class AsioPeriodicTimer : public std::enable_shared_from_this<AsioPeriodicTimer> {
public:
    using Callback = std::function<void()>;

    // 显式构造，需要传入绑定的 io_context
    explicit AsioPeriodicTimer(boost::asio::io_context& io_context)
        : timer_(io_context), is_running_(false) {}

    ~AsioPeriodicTimer() {
        Stop();
    }

    /// @brief 启动定时器
    /// @param interval_ms 间隔时间（毫秒）
    /// @param callback 回调函数，定时器到期时调用
    void Start(int interval_ms, Callback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_running_) {
            return; // 防止重复启动
        }

        is_running_ = true;
        interval_ = std::chrono::milliseconds(interval_ms);
        callback_ = std::move(callback);

        // 设置首次触发时间（当前时间 + 间隔时间）
        timer_.expires_after(interval_);

        // 投递异步任务，利用 shared_from_this 保证生命周期安全
        auto self = shared_from_this();
        timer_.async_wait([this, self](const boost::system::error_code& ec) {
            onTimer(ec);
        });
    }

    /// @brief 停止定时器
    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_running_) {
            return;
        }

        is_running_ = false;
        timer_.cancel(); // 取消当前所有等待的任务
    }

    /// @brief 检查定时器当前是否在运行
    bool IsRunning() const {
        return is_running_;
    }

private:
    /// @brief 定时器到期时调用的回调函数
    void onTimer(const boost::system::error_code& ec) {
        // 如果外部调用了 Stop() 或对象被取消，ec 会返回 operation_aborted，此时直接退出
        if (ec == boost::asio::error::operation_aborted || !is_running_) {
            return;
        }

        // 即使执行出错（如系统异常），也安全退出
        if (ec) {
            std::cerr << "Timer error: " << ec.message() << std::endl;
            is_running_ = false;
            return;
        }

        // 执行用户传入的回调函数
        if (callback_) {
            callback_();
        }

        // 核心：基于【上一次的理论到期时间】进行累加，完美消除任务执行耗时带来的时间漂移误差
        timer_.expires_at(timer_.expiry() + interval_);

        // 继续投递下一次的异步等待
        auto self = shared_from_this();
        timer_.async_wait([this, self](const boost::system::error_code& ec) {
            onTimer(ec);
        });
    }

    boost::asio::steady_timer timer_;
    std::chrono::milliseconds interval_{0};
    Callback callback_;
    bool is_running_;
    std::mutex mutex_;
};
