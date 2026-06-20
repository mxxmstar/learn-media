#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "node/i_node.h"
#include "node/sink_node.h"
#include "pipeline/pipeline_types.h"
#include "pusher/i_pusher.hpp"

namespace pipeline {

class PushStreamNode : public runtime::INode, public runtime::SinkNode<PacketMessage> {
public:
    PushStreamNode(PipelineOptions options, std::shared_ptr<PipelineState> state);

    /// @brief 初始化，这里直接返回 true，真正连接在 ensureConnected 中
    bool Init() override;

    /// @brief 启动推流节点，开始发送数据。实现逻辑在 ensureConnected 中
    bool Start() override;

    /// @brief 停止推流节点
    void Stop() override;

    /// @brief 反初始化，关闭推流连接，释放资源
    void Deinit() override;
    
    /// @brief 获取节点名称
    std::string Name() const override;

protected:
    void Process(PacketMessage packet) override;

private:
    /// @brief 初始化，配置推流参数，设置连接状态
    bool ensureConnected();

    PipelineOptions options_;   ///< 管线配置选项
    std::shared_ptr<PipelineState> state_;  ///< 管线状态
    std::unique_ptr<IPusher> pusher_;   ///< 推流器
    std::chrono::steady_clock::time_point last_connect_attempt_;    ///< 上次连接时间戳
    bool connected_{false};                                        ///< 连接状态
};

} // namespace pipeline
