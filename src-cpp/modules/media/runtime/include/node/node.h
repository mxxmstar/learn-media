#pragma once

/**
 * @file node.h
 * @brief Node 聚合头文件
 *
 * 包含节点体系的所有组件：
 *   - i_node.h:         INode 生命周期接口
 *   - source_node.h:    SourceNode<Out> 数据源 mixin
 *   - sink_node.h:      SinkNode<In> 数据汇 mixin
 *   - transform_node.h: TransformNode<In, Out> 变换节点 mixin
 */

#include "i_node.h"
#include "source_node.h"
#include "sink_node.h"
#include "transform_node.h"
