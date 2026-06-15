#pragma once

/**
 * @file i_node.h
 * @brief 节点生命周期接口
 *
 * INode 是框架中所有"节点"的生命周期抽象。注意它只管理
 * 生命周期，不处理数据——数据通过 InputPort / OutputPort 传递。
 *
 * 生命周期顺序：
 *   Construct → Init → Start → [Processing] → Stop → Deinit → Destroy
 *
 * 设计说明：
 *   - INode 与 SourceNode/SinkNode/TransformNode 分离，
 *     后者是"数据处理能力"的 mixin，前者是"生命周期"的接口。
 *   - Graph 在 Start 阶段依次调用 Init → Executor.Start → Start
 *   - 如果任何 Init 或 Start 失败，Graph 会回滚（Stop + Deinit）
 *   - Name() 用于日志、跟踪和调试
 */

#include <string>

namespace runtime {

class INode {
public:
    virtual ~INode() = default;

    /// @brief 初始化节点（分配资源、打开文件、连接设备等）
    /// @return true 成功，false 失败
    /// Init 在 Executor.Start 之前调用，确保资源就绪才启动线程
    virtual bool Init() = 0;

    /// @brief 启动节点（开始处理数据）
    /// @return true 成功，false 失败
    /// Start 在 Executor.Start 之后调用，此时线程池已在运行，
    /// 节点可以向 OutputPort 发送数据
    virtual bool Start() = 0;

    /// @brief 停止节点
    /// Stop 后不应该再处理数据。由 Graph::Stop 调用，顺序上
    /// 先 Stop 节点再 Close 边再 Close dispatch
    virtual void Stop() = 0;

    /// @brief 反初始化（释放 Init 分配的资源）
    virtual void Deinit() = 0;

    /// @brief 返回节点唯一标识名
    virtual std::string Name() const = 0;
};

}
