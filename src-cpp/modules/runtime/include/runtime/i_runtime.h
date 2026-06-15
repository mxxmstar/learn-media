#pragma once

/**
 * @file i_runtime.h
 * @brief 运行时顶层接口
 *
 * IRuntime 是 Media Pipeline Runtime 的最顶层抽象。
 *
 * 职责：
 *   - Start()：启动整个运行时（包括 Graph 和所有底层资源）
 *   - Stop()：停止运行时，确保有序关闭
 *
 * 当前实现由 Runtime 类（runtime.h）继承，
 * 未来可以有其他实现（如 RemoteRuntime 用于分布式部署）。
 */

namespace runtime {

class IRuntime {
public:
    virtual ~IRuntime() = default;

    /// @brief 启动运行时（包括所有节点、线程池和调度器）
    virtual bool Start() = 0;

    /// @brief 停止运行时（有序关闭所有资源）
    virtual void Stop() = 0;
};

}
