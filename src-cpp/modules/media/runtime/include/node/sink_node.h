#pragma once

/**
 * @file sink_node.h
 * @brief 数据汇节点 mixin
 *
 * SinkNode 是管线的终端消费者。它有一个 InputPort<In>，
 * 但没有 OutputPort。典型的 SinkNode 可以是渲染器、文件写入器、
 * 或网络发送器。
 *
 * 用法：
 *   class MySink : public INode, public SinkNode<DetectResult> {
 *       void Process(DetectResult data) override {
 *           draw_overlay(data);  // 消费数据但不产生新数据
 *       }
 *   };
 *
 * 注意：SinkNode 的构造函数中自动注册了 handler：
 *   input_.SetHandler([this](In data) { Process(std::move(data)); })
 * 这样 Drain 取到数据后通过 EdgeContext::consumer_ → InputPort::Receive
 * → handler → Process 最终调用子类的 Process。
 *
 * @tparam In 输入数据类型
 */

#include "port/input_port.h"

namespace runtime {

template<typename In>
class SinkNode {
public:
    SinkNode() {
        // 注册处理器：Drain 取到数据后调用 Process
        input_.SetHandler([this](In data) {
            Process(std::move(data));
        });
    }

    /// @brief 获取输入端口（Graph::Connect 时会调用此方法）
    InputPort<In>& Input() {
        return input_;
    }

protected:
    /// @brief 处理输入数据（纯虚，子类必须实现）
    virtual void Process(In data) = 0;

private:
    InputPort<In> input_;
};

}
