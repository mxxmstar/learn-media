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

节点的数据发送接口。`Send` 针对单下游 / 多下游做了区分：

- **单下游**：直接 `std::move` 转发，保持 move-only 语义
- **多下游**：编译期检查 `std::is_copy_constructible_v<T>`，每个 downstream 拷贝一份
- `Send` 返回 `true` 表示下游 `Accepted || DroppedOldest`（数据最终被处理）

```cpp
template<typename T>
class OutputPort {
    using Type = T;
    void AddTransport(shared_ptr<ITransport<T>>);
    bool Send(T data);    // 单下游 move，多下游 copy
};
```

---

### Layer 2: Transport — 传输通道

#### ITransport\<T\>

传输通道接口（v2 —— 全面改用 `std::optional<T>` 避免默认构造，`Send` 返回 `MailboxPushResult` 以精确统计每种背压结果）：

```cpp
template<typename T>
class ITransport {
    virtual MailboxPushResult Send(T data) = 0;    // 返回背压结果
    virtual std::optional<T> TryReceive() = 0;     // 非阻塞取，无默认构造
    virtual std::optional<T> Receive() = 0;        // 阻塞取，无默认构造
    virtual void Close() = 0;
    virtual bool Empty() const = 0;
    virtual std::size_t Size() const = 0;
};
```

`MailboxPushResult` 枚举：

```cpp
enum class MailboxPushResult { Accepted, DroppedNewest, DroppedOldest, Closed };
```

#### QueueTransport\<T\>

基于无锁 SPSC Mailbox 的异步传输，带背压策略和通知回调。`Send` 自动调用 `on_send_result_` 回调供 Graph 累加 metrics：

```cpp
template<typename T>
class QueueTransport : public ITransport<T> {
    explicit QueueTransport(size_t capacity = 64,
                            BackpressurePolicy policy = BackpressurePolicy::DropOldest);
    void SetNotifyCallback(NotifyCallback cb);
    void SetSendResultCallback(SendResultCallback cb);
    MailboxPushResult Send(T data) override;
    std::optional<T> TryReceive() override;    // → mailbox_.TryPopValue()
    std::optional<T> Receive() override;       // → mailbox_.WaitPopValue()
};
```

#### DirectTransport\<T\>

同线程同步直传，无缓冲。`Send()` 直接调用下游 `consumer_`，`TryReceive()` / `Receive()` 返回 `nullopt`（它不参与 Drain 循环）：

```cpp
template<typename T>
class DirectTransport : public ITransport<T> {
    void SetConsumer(Consumer consumer);               // 下游消费函数
    void SetSendResultCallback(SendResultCallback cb);
    MailboxPushResult Send(T data) override;           // 直接调 consumer_
    std::optional<T> TryReceive() override;            // 返回 nullopt
    std::optional<T> Receive() override;               // 返回 nullopt
    void Close() override;
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

接收 `shared_ptr<IEdgeContext>`，避免裸指针生命周期问题：

```cpp
class IScheduler {
    virtual bool Notify(std::shared_ptr<IEdgeContext> edge) = 0;
};
```

#### FIFOScheduler

收到通知后向目标 Executor Post Drain 任务，内部通过 `shared_ptr` 捕获 edge，确保任务延迟执行时 edge 仍然存活：

```cpp
bool Notify(std::shared_ptr<IEdgeContext> edge) override {
    auto* dst_ctx = edge->GetDestination();
    if (!dst_ctx || !dst_ctx->executor) return false;
    return dst_ctx->executor->Post([edge = std::move(edge)]() {
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

基于 `boost::asio::io_context` 的线程池实现。所有 mutable 状态（accepting / running / pending）打包到 `shared_ptr<State>` 中，lambda 捕获 `state` 而非裸 `this`。`Stop()` 会等待 `pending == 0` 确保所有已投递任务完成，条件变量通知：

```cpp
class AsioExecutor : public IExecutor {
    AsioExecutor(std::string name, std::string pool_name, size_t pool_size = 0);
    bool Start() override;
    void Stop() override;  // wait pending == 0
    bool Post(Task task) override;
};
```

内部 `State` 结构：

```cpp
struct State {
    std::atomic_bool accepting{false};
    std::atomic_bool running{false};
    std::atomic<size_t> pending{0};
    std::mutex mutex;
    std::condition_variable cv;
};
```

`Post()` 内 lambda 捕获 `state` 而非 `this`，任务完成时 `fetch_sub(1) == 1` 检查是否最后一个任务，只通知一次 `cv`：

```cpp
boost::asio::post(io_ctx, [state, task = std::move(task)]() mutable {
    try { task(); } catch (...) {}
    if (state->pending.fetch_sub(1) == 1) {
        std::lock_guard lock(state->mutex);
        state->cv.notify_all();
    }
});
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
    // 添加节点，编译期自动检测 Input()/Output() 端口并注册。返回 id（失败返回空串）
    template<typename T, typename... Args>
    std::string AddNode(std::string id,
                        std::shared_ptr<IExecutor> executor,
                        Args&&... args);

    // 连接节点，创建 EdgeContext<T> 并绑定端口。返回是否成功
    template<typename T>
    bool Connect(const std::string& src,
                 const std::string& dst,
                 TransportType type = TransportType::Queue,
                 size_t capacity = 64,
                 BackpressurePolicy policy = BackpressurePolicy::DropOldest);

    void SetScheduler(std::shared_ptr<IScheduler> scheduler);

    bool Start();       // → Init → Executor::Start → INode::Start
    void Stop();        // → Node::Stop → edge Close → Dispatch Close → Exec Stop → Deinit
    bool GetMetrics(id, NodeMetricsSnapshot&);
};
```

#### NodeContext

每个节点持有独立的序列化 `Dispatch` 队列。`Dispatch(task)` 在默认 `Serialized` 模式下将 task 排入队列，由单次 `Post` 依次消费，保证同一节点不同上游 edge 不会并发 `Process`：

```cpp
enum class NodeExecutionMode { Serialized, Concurrent };

struct NodeOptions {
    NodeExecutionMode execution_mode{NodeExecutionMode::Serialized};
};

struct NodeContext {
    std::string id;
    std::shared_ptr<INode> node;
    std::shared_ptr<IExecutor> executor;
    std::shared_ptr<NodeMetrics> metrics;
    NodeExecutionMode execution_mode{NodeExecutionMode::Serialized};

    std::unordered_map<std::type_index, std::any> input_ports;
    std::unordered_map<std::type_index, std::any> output_ports;

    bool Dispatch(Task task);      // 序列化/并发 模式
    void CloseDispatch();          // Stop 时关闭
    void OpenDispatch();           // Start 时打开
};
```

#### IEdgeContext / EdgeContext\<T\>

`TrySchedule()` 封装 CAS + 调度 + 异常处理，全部使用 `weak_ptr` 持有调度器和 metrics 以避免异步生命周期问题：

```cpp
class IEdgeContext : public std::enable_shared_from_this<IEdgeContext> {
    bool TrySchedule();            // CAS → scheduler.Notify → 失败复位 + rejected
    virtual void ExecuteDrain() = 0;
    virtual void Close() = 0;     // 关闭 transport
    virtual bool Empty() const = 0;

    std::atomic<bool> scheduled{false};
    std::weak_ptr<IScheduler> scheduler_;
    std::weak_ptr<NodeMetrics> dst_metrics_;
    NodeContext* dst_{nullptr};
};

template<typename T>
class EdgeContext : public IEdgeContext {
    std::shared_ptr<ITransport<T>> transport;
    Consumer<T> consumer_;

    void ExecuteDrain() override {
        struct ScheduleReset {
            EdgeContext* edge;
            ~ScheduleReset() {            // RAII：无论异常都复位 scheduled
                edge->scheduled.store(false);
                if (!edge->transport->Empty()) edge->TrySchedule();
            }
        } reset{this};

        while (transport) {
            auto msg = transport->TryReceive();   // 非阻塞，队列空立刻退出
            if (!msg.has_value()) break;

            auto* dst = GetDestination();
            dst->Dispatch([consumer = consumer_, metrics = dst_metrics_, msg = std::move(*msg)]() {
                try {
                    consumer(std::move(msg));
                    if (auto m = metrics.lock()) m->processed.fetch_add(1);
                } catch (...) {
                    if (auto m = metrics.lock()) m->errors.fetch_add(1);
                }
            });
        }
    }
};
```

`ScheduleReset` RAII 守卫确保：
- `scheduled` 无论如何都会复位（正常 drain 完 / 异常 / 提前 break）
- 如果复位后 transport 仍有数据，立即 `TrySchedule()` 继续消费
- `consumer_` 的异常被捕获到 `errors` 计数器，不会影响 scheduled 状态

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
│    ├─ ScheduleReset RAII guard (即使异常也复位 scheduled)                │
│    │                                                                     │
│    ├─ while (transport.TryReceive(packet))   ← 非阻塞 TryPopValue       │
│    │      → dst->Dispatch([consumer, metrics, msg]() {                  │
│    │          try { consumer(msg); metrics->processed++; }               │
│    │          catch(...) { metrics->errors++; }                          │
│    │      })    ← 序列化/并发 由 NodeContext 的 Dispatch 模式决定        │
│    │             ↑ 注意：Direct 不走此路径，Send 内直接调 consumer_     │
│    │                                                                     │
│    ├─ ~ScheduleReset(): scheduled.store(false)                          │
│    └─ if (!transport.Empty()) { TrySchedule() → CAS → Notify }          │
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

---

## 修复记录

### P0 — 死锁 / 空转 / 安全性
#### 20260607：
| 问题 | 修复 |
|------|------|
| **`ExecuteDrain()` 阻塞/忙等** | 改用 `TryReceive()` 非阻塞消费当前批次，队列空立刻退出。RAII `ScheduleReset` 在析构时复位 `scheduled` 并在必要时重新 Notify，彻底避免 spin 和永久阻塞 |
| **裸指针异步捕获** | `Scheduler::Notify` 改为接收 `shared_ptr<IEdgeContext>`；lambda 捕获 `shared_ptr` 而非 `this*`，确保任务延迟执行时 edge 仍存活 |
| **`AsioExecutor::Post()` 捕获裸 `this`** | 所有 mutable 状态（accepting / running / pending）打包到 `shared_ptr<State>`，lambda 捕获 `state`。`Stop()` 通过 `cv.wait(..., pending==0)` 等待所有已投递任务完成 |

### P1 — 状态卡死 / 错误处理

| 问题 | 修复 |
|------|------|
| **scheduled 永久卡死** | `TrySchedule()` 调度失败时立即复位 `scheduled` 并递增 `rejected`。RAII `ScheduleReset` 保证 `scheduled` 在异常或正常退出时均被复位 |
| **consumer 异常导致 scheduled 未复位** | `ExecuteDrain()` 内 `consumer_` 的异常被 `try-catch` 并计入 `errors`，`ScheduleReset` 析构时总会复位 scheduled |
| **DirectTransport 假接入** | `Connect<T>(Direct)` 现在实际创建 `DirectTransport<T>`，`Send` 内部直接调用下游 `consumer_`，不再伪装成 Queue |
| **`Connect` 无返回值** | 改为 `bool Connect(...)`，port 不匹配、节点不存在、scheduler 未设置时返回 `false` |
| **`AddNode` 重复 id 覆盖** | 重复 id 返回空字符串 `{}`，不覆盖已有节点 |
| **`Start` 不检查返回值** | 逐阶段检查 `Init` / `Executor::Start` / `Node::Start` 返回值，失败后部分回滚已启动的 executor |

### P2 — 完备性 / 泛型 / 语义

| 问题 | 修复 |
|------|------|
| **Metrics 未全量计数** | `enqueued` / `processed` / `dropped` / `rejected` / `errors` 五个计数器全部正确递增。`Send` 结果回调 `count_send_result` 精确区分 Accepted / DroppedOldest（enqueued+1） / DroppedNewest（只计 dropped） / Closed（计 rejected） |
| **背压语义不准确** | `ITransport::Send()` 返回 `MailboxPushResult`，DroppedNewest 不再混为 Accepted。`OutputPort::IsAccepted` 仅将 `Accepted` 和 `DroppedOldest`（因旧帧被淘汰）视为成功 |
| **Drain 要求 `T{}` 默认构造** | `TryReceive()` 返回 `std::optional<T>`，彻底消除默认构造依赖 |
| **SPSC/MPMC DropOldest 和对多下游 Clear 要求默认构造** | DropOldest 和 Clear 改用非默认构造路径（SPSC 用 `queue_.clear()` / MPMC 用 `pop` 循环） |
| **单下游 move-only 无法移动发送** | `OutputPort::Send` 对单下游做 `std::move(data)`，多下游才要求 `is_copy_constructible_v<T>` |
| **未实现 Node 级串行化** | `NodeContext::Dispatch` 提供 `Serialized`（默认）和 `Concurrent` 两种模式。`Serialized` 模式下同一节点的多个上游 edge 不会并发 `Process` |

---

## 视频流水线里目前还需要注意的缺陷：

```plaintext
同一个 node 如果有多条输入 edge，仍可能被多个 edge 并发调用，需要 node 级 strand/serialized executor。 √
port 只按 type_index 匹配，同类型多输入/多输出不好表达，需要命名端口。
多下游 fan-out 对大帧/move-only 对象仍不理想，视频帧建议统一 shared_ptr 或引入 clone/zero-copy fanout 策略。
背压只是单 edge 局部策略，还缺全链路水位、限流、关键帧优先、丢非关键帧策略。
缺少 per-stream 顺序、时间戳 watermark、同步节点聚合策略。
GPU 推理还缺 batching、优先级、队列延迟指标和超时丢帧策略。
Stop 目前偏“停止并关闭”，还没有可配置的 drain/flush/discard shutdown policy。
```

## 构建

```cmake
target_link_libraries(your_target PRIVATE media_runtime)
```

依赖：`common_lib`（无锁队列）、`Boost::asio`（线程池）。

CMakeLists.txt 已配置 C++20 标准。
