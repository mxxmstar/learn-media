# Media Runtime Framework

## 概述

Media Runtime 是一个轻量级、高性能的流式数据处理框架，专为媒体处理和 AI 推理管线设计。核心设计理念：

- **分层解耦**：Message / Port & Transport / Node / Executor / Scheduler / Graph / Runtime 各层职责清晰
- **类型安全**：端口（Port）通过模板参数保证数据类型安全，Graph 在编译期检测端口类型
- **事件驱动**：数据到达时通过 Scheduler 调度 Executor 执行，无轮询开销
- **无锁高性能**：节点间传输使用无锁 SPSC/MPMC 队列，CAS 控制调度状态

---

## 架构分层

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 6: Runtime                                                │
│  系统生命周期管理，创建 Executor，持有 Graph                       │
├─────────────────────────────────────────────────────────────────┤
│ Layer 5: Graph                                                  │
│  AddNode<T> / Connect<T> / Start / Stop / GetMetrics            │
│  NodeContext { node, executor, metrics, ports }                 │
│  EdgeContext<T> { transport, consumer, scheduled, Drain }       │
├─────────────────────────────────────────────────────────────────┤
│ Layer 4: Executor                                               │
│  IExecutor { Start, Stop, Post(task) }                          │
│  AsioExecutor (Boost.Asio 线程池)                               │
│  AsioIOContextPoolManager (多命名线程池管理)                     │
├─────────────────────────────────────────────────────────────────┤
│ Layer 3: Scheduler                                              │
│  IScheduler { Notify(edge) }                                    │
│  FIFOScheduler / DropFrameScheduler                             │
├──────────────────────────┬──────────────────────────────────────┤
│ Layer 2: Node             │ Layer 2: Transport & Port            │
│ INode (生命周期)          │ ITransport<T> (传输通道)             │
│ SourceNode<Out>           │ QueueTransport / DirectTransport     │
│ SinkNode<In>              │ InputPort<T> (接收端口)             │
│ TransformNode<In,Out>     │ OutputPort<T> (发送端口)            │
│                           │ IMailBox / SPSC / MPMC (无锁队列)    │
├──────────────────────────┴──────────────────────────────────────┤
│ Layer 1: Message                                                │
│  MediaPacket / MediaFrame / AnyMessage (std::variant 预留)      │
└─────────────────────────────────────────────────────────────────┘
```

### 依赖方向

```
Runtime → Graph → Scheduler → Executor
                → Node → Port → Transport → Mailbox
                                       → Message
```

上层依赖下层，下层绝不反向依赖上层。

---

## 各层详解

### Layer 1: Message — 数据类型

定义管线上流动的数据。目前支持媒体层面的强类型：

```cpp
// media_packet.hpp / media_frame.hpp
MediaPacket   // 编码数据包（h264, h265, aac...）
MediaFrame    // 原始图像帧（YUV, RGB...）
```

预留 `AnyMessage`（`std::variant<std::monostate, MediaFrame, MediaPacket>`）供后续扩展。

---

### Layer 2: Port — 端口

#### InputPort\<T\>

节点的数据接收接口：

```cpp
template<typename T>
class InputPort {
    using Type = T;                          // 数据类型标记
    void Bind(shared_ptr<ITransport<T>>);    // 绑定到传输通道
    void SetHandler(function<void(T)>);       // 设置数据处理回调
    void Receive(T data);                     // 收到数据后调用 handler
};
```

#### OutputPort\<T\>

节点的数据发送接口：

```cpp
template<typename T>
class OutputPort {
    using Type = T;
    void AddTransport(shared_ptr<ITransport<T>>);  // 注册下游传输通道
    void Send(T data);                              // 广播数据到所有下游
};
```

---

### Layer 2: Transport — 传输通道

#### ITransport\<T\>

传输通道纯虚接口：

```cpp
template<typename T>
class ITransport {
    virtual bool Send(T data) = 0;
    virtual bool Receive(T& data) = 0;
};
```

#### QueueTransport\<T\>

基于无锁 SPSC Mailbox 的异步传输，带背压策略和通知回调：

```cpp
template<typename T>
class QueueTransport : public ITransport<T> {
    // 构造参数：容量，背压策略（DropOldest/DropNewest/Block/Unbounded）
    explicit QueueTransport(size_t capacity = 64,
                            BackpressurePolicy policy = BackpressurePolicy::DropOldest);

    void SetNotifyCallback(NotifyCallback cb);  // 数据入队时的通知回调
    bool Send(T data) override;                  // mailbox.Push + notify
    bool Receive(T& data) override;              // mailbox.WaitPop
};
```

#### DirectTransport\<T\>

同线程同步直传，无缓冲：

```cpp
template<typename T>
class DirectTransport : public ITransport<T> {
    bool Send(T data) override;      // 直接覆盖 buffer
    bool Receive(T& data) override;  // 读取 buffer
};
```

#### IMailBox 体系

无锁 SPSC/MPSC 队列，提供背压支持：

```
IMailBox<T> (接口)
├── SPSCMailBox<T> (boost::lockfree::spsc_queue 实现)
└── MPMCMailBox<T> (moodycamel::ConcurrentQueue 实现)
```

```cpp
enum class BackpressurePolicy { Block, DropNewest, DropOldest, Unbounded };
enum class MailboxPushResult { Accepted, DroppedNewest, DroppedOldest, Closed };
```

> `QueueTransport` 目前使用 SPSCMailBox。若需多生产者场景，可改为 MPMCMailBox。

---

### Layer 2: Node — 节点

#### INode — 生命周期接口

```cpp
class INode {
    virtual bool Init() = 0;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void Deinit() = 0;
    virtual std::string Name() const = 0;
};
```

#### SourceNode\<Out\> — 数据源节点

自主产生数据，无输入端口：

```cpp
template<typename Out>
class SourceNode {
    OutputPort<Out>& Output();    // 暴露输出端口
protected:
    void Emit(Out data);          // 发送数据到下游
};
```

#### SinkNode\<In\> — 接收节点

接收并处理数据，无输出端口：

```cpp
template<typename In>
class SinkNode {
    InputPort<In>& Input();       // 暴露输入端口
    // 构造时自动设置 handler → this->Process(data)
protected:
    virtual void Process(In data) = 0;  // 业务处理
};
```

#### TransformNode\<In, Out\> — 变换节点

接收数据、处理、发送结果：

```cpp
template<typename In, typename Out>
class TransformNode {
    InputPort<In>& Input();       // 输入端口
    OutputPort<Out>& Output();    // 输出端口
protected:
    virtual void Process(In data) = 0;  // 处理输入
    void Emit(Out data);                 // 发送输出
};
```

---

### Layer 3: Scheduler — 调度器

#### IScheduler

```cpp
class IScheduler {
    virtual void Notify(IEdgeContext* edge) = 0;
};
```

#### FIFOScheduler

收到通知后立即向目标 Executor Post Drain 任务：

```cpp
void Notify(IEdgeContext* edge) override {
    edge->GetDestination()->executor->Post([edge]() {
        edge->ExecuteDrain();
    });
}
```

#### DropFrameScheduler

接口同 FIFOScheduler，可在此实现更复杂的丢帧策略。

---

### Layer 4: Executor — 执行器

#### IExecutor

```cpp
class IExecutor {
    using Task = std::function<void()>;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool Post(Task task) = 0;
};
```

#### AsioExecutor

基于 `boost::asio::io_context` 的线程池实现：

```cpp
class AsioExecutor : public IExecutor {
    // pool_name: 命名线程池（"general", "cpu", "inference", "io" 等）
    // pool_size: 线程数（0 = hardware_concurrency）
    AsioExecutor(std::string name, std::string pool_name, size_t pool_size = 0);
};
```

#### AsioIOContextPoolManager

全局单一实例的命名线程池管理器：

```cpp
AsioIOContextPoolManager::Initialize("cpu", 4);          // 创建 4 线程的 cpu 池
AsioIOContextPoolManager::Initialize("inference", 1);     // 创建 1 线程的 inference 池
auto& pool = AsioIOContextPoolManager::GetInstance("cpu"); // 获取
AsioIOContextPoolManager::StopAll();                       // 程序退出时清理
```

命名池会自动创建（`AsioExecutor` 构造时调用 `Initialize`），避免不同 Executor 互相阻塞。

---

### Layer 5: Graph — 拓扑管理

#### Graph 核心 API

```cpp
class Graph {
    // 添加节点，编译期自动检测 Input()/Output() 端口并注册
    template<typename T, typename... Args>
    void AddNode(std::string id,
                 std::shared_ptr<IExecutor> executor,
                 Args&&... args);

    // 连接节点，创建 EdgeContext<T> 并绑定端口
    template<typename T>
    void Connect(const std::string& src,
                 const std::string& dst,
                 TransportType type = TransportType::Queue,
                 size_t capacity = 64,
                 BackpressurePolicy policy = BackpressurePolicy::DropOldest);

    void SetScheduler(std::shared_ptr<IScheduler> scheduler);

    bool Start();       // → Init → Executor::Start → INode::Start
    void Stop();        // → INode::Stop → Executor::Stop → INode::Deinit
    bool GetMetrics(id, NodeMetricsSnapshot&);
};
```

#### NodeContext

```cpp
struct NodeContext {
    std::string id;
    std::shared_ptr<INode> node;
    std::shared_ptr<IExecutor> executor;
    std::shared_ptr<NodeMetrics> metrics;
    // 端口访问表（类型擦除，Connect<T> 时查找）
    std::unordered_map<std::type_index, std::any> input_ports;
    std::unordered_map<std::type_index, std::any> output_ports;
};
```

#### EdgeContext\<T\>

```cpp
template<typename T>
class EdgeContext : public IEdgeContext {
    std::shared_ptr<QueueTransport<T>> transport;  // 数据通道
    Consumer<T> consumer_;                         // 类型安全的消费者
    NodeContext* dst_;                             // 下游节点上下文
    IScheduler* scheduler_;                        // 调度器

    // 核心：从 transport 读取所有数据，调用 consumer_
    void ExecuteDrain() override {
        T msg;
        while (transport->Receive(msg)) {
            consumer_(std::move(msg));
        }
        scheduled.store(false);
        if (!transport->Empty()) { CAS → Notify }
    }

    // CAS 防止重复投递 Drain 任务
    std::atomic<bool> scheduled{false};
};
```

#### NodeMetrics

```cpp
struct NodeMetrics {
    std::atomic<uint64_t> enqueued;   // 入队数
    std::atomic<uint64_t> processed;  // 处理成功数
    std::atomic<uint64_t> dropped;    // 丢弃数
    std::atomic<uint64_t> rejected;   // 拒绝数（Mailbox 关闭后）
    std::atomic<uint64_t> errors;     // 异常数

    NodeMetricsSnapshot Snapshot() const;
};
```

---

### Layer 6: Runtime — 运行时

```cpp
class Runtime {
    Graph& GetGraph();                            // 获取 Graph 实例
    shared_ptr<AsioExecutor> CreateExecutor(       // 快捷创建命名 Executor
        string name, string pool_name, size_t pool_size = 0);
    bool Start();                                  // 启动整条管线
    void Stop();                                   // 停止整条管线
    void ShutdownAllPools();                       // 清理所有 Asio 线程池
};
```

---

## 运行时数据流

### 完整链路时序

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Graph::Connect<MediaPacket>("decoder", "infer", Queue)                   │
│                                                                          │
│  1. 创建 EdgeContext<MediaPacket>                                        │
│  2. 创建 QueueTransport<MediaPacket>(64, DropOldest)                     │
│  3. 设置 consumer_ = [infer.Input()](msg) { input.Receive(msg); }       │
│  4. 设置 notify callback:                                                │
│       transport.Send → mailbox.Push → CAS scheduled → scheduler.Notify  │
│  5. decoder.Output().AddTransport(transport)                             │
│  6. infer.Input().Bind(transport)                                        │
└─────────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ 运行时（Decoder 的 Executor 线程上）                                      │
│                                                                          │
│  SourceNode::Emit(packet)                                                │
│    → OutputPort::Send(packet)                                            │
│      → QueueTransport::Send(packet)                                      │
│        ├─ mailbox.Push(packet)          ← 无锁入队                       │
│        └─ notify_()                                                     │
│             └─ CAS scheduled: false→true                                 │
│                  └─ scheduler.Notify(edge)                               │
│                       └─ infer.executor.Post([edge] {                    │
│                              edge->ExecuteDrain();                       │
│                          })                                              │
└─────────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Infer 的 Executor 线程上：                                                │
│                                                                          │
│  EdgeContext::ExecuteDrain()                                             │
│    ├─ while (transport.Receive(packet))   ← mailbox.Pop                 │
│    │      consumer_(packet)                                              │
│    │        → InputPort<MP>::Receive(packet)                            │
│    │          → handler_(packet)                                         │
│    │            → InferNode::Process(packet)        ← 业务逻辑           │
│    │              → Emit(result)                    ← 可能发往下游       │
│    │                                                                     │
│    ├─ scheduled.store(false)                          ← CAS 复位         │
│    └─ if (!transport.Empty()) { CAS true → Notify }   ← 继续消费        │
└─────────────────────────────────────────────────────────────────────────┘
```

### 关键设计点

- **CAS scheduled 防止重复调度**：一个 Edge 同时最多只有一个 Drain 任务在 Executor 队列中。多路视频场景下，即使多个上游 SourceNode 同时发送，也只会触发一次 Drain，由 Drain 循环一次性取完 mailbox 中所有数据
- **Drain 循环**：while 循环确保一次性消费完 mailbox 中积压的所有消息，避免频繁 Post 任务
- **背压机制**：当 mailbox 满时，`DropOldest` 策略丢弃最旧帧，保证实时性
- **类型安全**：`EdgeContext<T>` 的 `consumer_` 在 `Connect<T>` 时确定类型，Drain 循环中类型不会丢失

---

## 使用指南

### 1. 创建节点

```cpp
// 继承 INode + SourceNode / SinkNode / TransformNode

class RtspSource : public INode, public SourceNode<MediaPacket> {
public:
    bool Init() override { /* 初始化拉流 */ return true; }
    bool Start() override {
        thread_ = std::thread([this]() {
            while (running_) {
                MediaPacket pkt = puller_.Read();
                Emit(std::move(pkt));    // 发送到下游
            }
        });
        return true;
    }
    void Stop() override { running_ = false; if (thread_.joinable()) thread_.join(); }
    void Deinit() override { puller_.Close(); }
    std::string Name() const override { return "rtsp_source"; }
private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    // ... puller
};

class VideoDecoder : public INode, public TransformNode<MediaPacket, MediaFrame> {
public:
    void Process(MediaPacket pkt) override {
        MediaFrame frame = decode(pkt);
        Emit(std::move(frame));
    }
    // ...
};

class InferenceNode : public INode, public SinkNode<MediaFrame> {
public:
    void Process(MediaFrame frame) override {
        auto result = model_.Infer(frame);
        // 处理结果...
    }
    // ...
};
```

### 2. 构建管线

```cpp
runtime::Runtime rt;

// 创建 Executor
auto src_exec = rt.CreateExecutor("source", "io", 1);
auto dec_exec = rt.CreateExecutor("decoder", "cpu", 4);
auto inf_exec = rt.CreateExecutor("inference", "inference", 1);

auto& g = rt.GetGraph();

// 添加节点
g.AddNode<RtspSource>("source", src_exec);
g.AddNode<VideoDecoder>("decoder", dec_exec);
g.AddNode<InferenceNode>("infer", inf_exec);

// 连接节点（编译期检查 MediaPacket 端口匹配）
g.Connect<MediaPacket>("source", "decoder", TransportType::Queue, 64);
g.Connect<MediaFrame>("decoder", "infer", TransportType::Queue, 32);

// 启动
rt.Start();

// ... 运行 ...

// 停止
rt.Stop();
rt.ShutdownAllPools();

// 获取统计
runtime::NodeMetricsSnapshot metrics;
g.GetMetrics("decoder", metrics);
// metrics.processed, metrics.dropped, ...
```

### 3. 测试节点

```cpp
// 为测试编写 Mock Source 和 Collector Sink
class MockSource : public INode, public SourceNode<MediaPacket> {
    // 生成测试数据
};

class CollectorSink : public INode, public SinkNode<MediaPacket> {
public:
    std::vector<MediaPacket> received;
    void Process(MediaPacket pkt) override {
        received.push_back(std::move(pkt));
    }
};

TEST(DecoderTest, Basic) {
    Runtime rt;
    auto exec = rt.CreateExecutor("test", "general", 1);
    auto& g = rt.GetGraph();

    g.AddNode<MockSource>("src", exec);
    g.AddNode<VideoDecoder>("decoder", exec);
    g.AddNode<CollectorSink>("sink", exec);

    g.Connect<MediaPacket>("src", "decoder");
    g.Connect<MediaPacket>("decoder", "sink");

    rt.Start();
    std::this_thread::sleep_for(1s);
    rt.Stop();

    // 验证 CollectorSink.received
}
```

---

## 管线示例

### 单路拉流 + 解码 + 推理

```
                     QueueTransport(64)
  RtspSource ──────────────────────────────► VideoDecoder
  (io executor)       MediaPacket             (cpu executor)
                                                 │
                                          QueueTransport(32)
                                                 │
                                                 ▼
                                            InferenceNode
                                          (inference executor)
```

### 多路视频

```
RtspSource_1 ──┐
               ├─► Mixer ──► Decoder ──► Infer
RtspSource_2 ──┘
```

每个 Source 同一时刻只有一个 Drain 任务在运行，`scheduled` CAS 保证不重复调度。

---

## 目录结构

```
runtime/include/
├── message/
│   └── any_message.h
├── port/
│   ├── input_port.h
│   └── output_port.h
├── transport/
│   ├── i_transport.h
│   ├── direct_transport.h
│   ├── queue_transport.h
│   ├── i_mailbox.h
│   ├── spsc_mailbox.h
│   ├── mpmc_mailbox.h
│   ├── mailbox.h
│   └── transport.h
├── node/
│   ├── i_node.h
│   ├── source_node.h
│   ├── sink_node.h
│   ├── transform_node.h
│   └── node.h
├── executor/
│   ├── i_executor.h
│   ├── asio_executor.h
│   └── asio_io_context_pool.h
├── scheduler/
│   ├── i_scheduler.h
│   ├── fifo_scheduler.h
│   └── dropframe_scheduler.h
├── graph/
│   ├── node_context.h
│   ├── edge_context.h
│   └── graph.h
└── runtime/
    ├── i_runtime.h
    └── runtime.h
```

## 构建

```cmake
target_link_libraries(your_target PRIVATE media_runtime)
```

依赖：`common_lib`（无锁队列）、`Boost::asio`（线程池）。

CMakeLists.txt 已配置 C++20 标准。
