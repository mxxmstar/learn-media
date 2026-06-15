#pragma once

/**
 * @file mailbox.h
 * @brief Mailbox 聚合头文件及工厂函数
 *
 * 包含 Mailbox 体系的所有组件，并提供 CreateMailBox 工厂函数。
 *
 * IMailBox<T> 是框架中"数据通道"的底层存储单元，
 * 由 QueueTransport 内部持有。CreateMailBox 简化了 SPSC/MPMC
 * 的选择：
 *
 *   auto mb = CreateMailBox<MediaFrame>(MailBoxKind::SPSC, 64);
 */

#include "transport/i_mailbox.h"
#include "transport/spsc_mailbox.h"
#include "transport/mpmc_mailbox.h"

#include <memory>

namespace runtime {

/**
 * @brief 创建 Mailbox 实例的工厂函数
 * @tparam T 元素类型
 * @param kind Mailbox 类型：SPSC（默认，无锁高性能）或 MPMC（多生产者安全）
 * @param capacity 容量（0 表示无界，仅 MPMC 模式下有意义）
 * @return Mailbox 实例的唯一指针
 */
template <typename T>
std::unique_ptr<IMailBox<T>> CreateMailBox(MailBoxKind kind, std::size_t capacity) {
    if (kind == MailBoxKind::MPMC) {
        return std::make_unique<MPMCMailBox<T>>(capacity);
    }

    return std::make_unique<SPSCMailBox<T>>(capacity);
}

}
