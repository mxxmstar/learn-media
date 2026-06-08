#pragma once

/**
 * @file any_message.h
 * @brief 通用消息类型定义
 *
 * 为未来的"通用类型管线"预留。
 *
 * 当前框架使用强类型 InputPort<T> / OutputPort<T>，
 * 管线在编译期确定数据类型。AnyMessage 是弱类型的备选方案，
 * 适用于运行时类型不确定的场景（如动态管线）。
 *
 * AnyMessage 基于 std::variant<MediaFrame, MediaPacket, ...>，
 * 提供类型安全访问：
 *   - Is<T>()：检查是否持有 T 类型
 *   - Get<T>()：获取 T 类型的引用
 *   - Visit(func)：访问者模式遍历
 *
 * 当前系统中未使用 AnyMessage 作为主要传输类型，
 * 所有 Transport 仍然使用具体类型 T。
 */

#include <variant>

#include "defines/media_frame.hpp"
#include "defines/media_packet.hpp"

namespace runtime {

/// 消息变体类型列表
using MessageVariant = std::variant<
    std::monostate,   // 占位（空消息）
    MediaFrame,       // 原始图像帧（YUV, RGB 等）
    MediaPacket       // 编码数据包（H.264, H.265 等）
>;

/// @brief 通用消息信封
class AnyMessage {
public:
    template <typename T>
    AnyMessage(T&& data) : data_(std::forward<T>(data)) {}

    /// @brief 检查消息是否持有指定类型
    template <typename T>
    bool Is() const { return std::holds_alternative<T>(data_); }

    /// @brief 获取指定类型的引用（调用者需先调用 Is<T>() 检查）
    template <typename T>
    const T& Get() const { return std::get<T>(data_); }

    /// @brief 访问者模式——调度不同数据类型的处理
    template <typename Func>
    auto Visit(Func&& f) const {
        return std::visit(std::forward<Func>(f), data_);
    }

private:
    MessageVariant data_;
};

}
