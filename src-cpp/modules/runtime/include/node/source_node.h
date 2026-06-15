#pragma once

/**
 * @file source_node.h
 * @brief 数据源节点 mixin
 *
 * SourceNode 是管线中的数据生产者。它有一个 OutputPort<Out>，
 * 但没有 InputPort。典型的 SourceNode 可以是 RTSP 源、
 * 摄像头源、文件读取器或模拟数据生成器。
 *
 * 用法：
 *   class MySource : public INode, public SourceNode<MediaFrame> {
 *       bool Start() override {
 *           // 启动读取线程或注册回调
 *           while (running) {
 *               MediaFrame frame = read_frame();
 *               Emit(std::move(frame));  // 发送到 OutputPort
 *           }
 *       }
 *   };
 *
 * 线程安全：Emit 可以在任意线程调用，OutputPort::Send 内部是线程安全的
 *          （QueueTransport 的 SPSCMailBox Push 是无锁的）。
 *
 * @tparam Out 输出数据类型
 */

#include "port/output_port.h"

namespace runtime {

template<typename Out>
class SourceNode {
public:
    /// @brief 获取输出端口（Graph::Connect 时会调用此方法）
    OutputPort<Out>& Output() {
        return output_;
    }

protected:
    /// @brief 发送数据到下游。source node 唯一的数据输出方式
    void Emit(Out data) {
        output_.Send(std::move(data));
    }

private:
    OutputPort<Out> output_;
};

}
