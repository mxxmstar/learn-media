#pragma once

/// @file any_message.h
/// @brief 通用消息类型定义
///
/// 定义通用消息数据类型

#include <variant>

#include "defines/media_frame.hpp"
#include "defines/media_packet.hpp"

namespace runtime {

// 1. 通用消息数据类型
using MessageVariant = std::variant<
    std::monostate, // 0
    MediaFrame,   // 原始图像帧（yuv,rgb）
    MediaPacket // 编码数据包（h264,h265）
>;

// 2. 通用消息信封
class AnyMessage {
public:
    template <typename T>
    AnyMessage(T&& data) : data_(std::forward<T>(data)) {}

    // 类型检查
    template <typename T>
    bool Is() const { return std::holds_alternative<T>(data_); }

    // 获取引用
    template <typename T>
    const T& Get() const { return std::get<T>(data_); }
    
    // 访问者模式
    template <typename Func>
    auto Visit(Func&& f) const {
        return std::visit(std::forward<Func>(f), data_);
    }

private:
    MessageVariant data_;
};

} // namespace runtime