#pragma once

/**
 * @file input_port.h
 * @brief 输入端口——节点接收数据的接口
 *
 * 每个节点可以有零个或多个 InputPort<T>。InputPort 不直接参与
 * Transport 的数据读取（Drain 层负责轮询 Transport），而是通过
 * SetHandler 注册一个回调——当 Drain 取到数据后，回调被调用。
 *
 * 设计说明：
 *   - InputPort 仅仅是"声明"，持有 ITransport<T> 引用的目的是
 *     在将来可能的"端口级控制"中使用（如暂停接收）。
 *   - 当前数据流不经过 InputPort 的 Receive 方法，而是由
 *     EdgeContext::ExecuteDrain → NodeContext::Dispatch →
 *     node.Process(data) 完成。InputPort 的 Receive 设计为
 *     备用的直接注入接口（如 DirectTransport 场景）。
 *
 * @tparam T 该端口接收的数据类型
 */

#include <functional>
#include <memory>

namespace runtime {

template<typename T>
class ITransport;

template<typename T>
class InputPort {
public:
    using Type = T;
    /// 数据处理器签名。Drain 取到数据后最终调用 handler_(data)
    using Handler = std::function<void(T)>;

    /// @brief 绑定传输通道。由 Graph::Connect 调用
    void Bind(std::shared_ptr<ITransport<T>> transport) {
        transport_ = std::move(transport);
    }

    /// @brief 注册数据处理函数。由节点构造函数或 Init 时设置
    void SetHandler(Handler handler) {
        handler_ = std::move(handler);
    }

    /// @brief 直接接收数据（当前：实际不经过此路径，留作 Future 扩展）
    void Receive(T data) {
        if (handler_) {
            handler_(std::move(data));
        }
    }

private:
    std::shared_ptr<ITransport<T>> transport_;  ///< 绑定的传输通道
    Handler handler_;                            ///< 数据处理回调
};

}
