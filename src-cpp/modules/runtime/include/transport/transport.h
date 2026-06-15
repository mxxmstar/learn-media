#pragma once

/**
 * @file transport.h
 * @brief Transport 聚合头文件
 *
 * 包含 Transport 体系的所有组件：
 *   - i_transport.h:     ITransport<T> 抽象接口
 *   - queue_transport.h: QueueTransport<T> 异步通道（默认）
 *   - direct_transport.h: DirectTransport<T> 同步通道
 */

#include "i_transport.h"
#include "direct_transport.h"
#include "queue_transport.h"
