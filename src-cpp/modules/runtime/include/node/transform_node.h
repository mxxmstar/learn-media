#pragma once

/**
 * @file transform_node.h
 * @brief 变换节点 mixin（管线中最常用）
 *
 * TransformNode 是管线中最常见的节点类型：接收一个类型的数据，
 * 处理后产生另一个类型的数据。例如：
 *   - Decoder: MediaPacket → MediaFrame
 *   - Resize: MediaFrame → MediaFrame（同一类型）
 *   - Inference: MediaFrame → DetectResult（不同类型）
 *
 * 用法：
 *   class MyDecoder : public INode, public TransformNode<MediaPacket, MediaFrame> {
 *       void Process(MediaPacket pkt) override {
 *           auto frame = decode(pkt);
 *           Emit(std::move(frame));
 *       }
 *   };
 *
 * 数据流路径：
 *   OutputPort<In>::Send → Transport → EdgeContext::ExecuteDrain
 *   → NodeContext::Dispatch → InputPort::Receive → handler → Process(this)
 *   → Process 内部调用 Emit → OutputPort<Out>::Send → 下一个节点
 *
 * @tparam In  输入数据类型
 * @tparam Out 输出数据类型
 */

#include "port/input_port.h"
#include "port/output_port.h"

namespace runtime {

template<typename In, typename Out>
class TransformNode {
public:
    TransformNode() {
        // 注册输入处理器
        input_.SetHandler([this](In data) {
            Process(std::move(data));
        });
    }

    /// @brief 获取输入端口
    InputPort<In>& Input() {
        return input_;
    }

    /// @brief 获取输出端口
    OutputPort<Out>& Output() {
        return output_;
    }

protected:
    /// @brief 处理输入数据并产生输出（纯虚，子类实现核心业务逻辑）
    virtual void Process(In data) = 0;

    /// @brief 发送处理结果到下游
    void Emit(Out data) {
        output_.Send(std::move(data));
    }

private:
    InputPort<In> input_;
    OutputPort<Out> output_;
};

}
