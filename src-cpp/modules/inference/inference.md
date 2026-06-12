# inference

## 整体架构
                        InferenceNode
                               │
                               ▼
                       InferenceSession
                      ╱                ╲
                     ▼                  ▼
                 IModel          IInferenceEngine
                     │                  │
                     ▼                  ▼
              TensorFrame      TensorFrame
                     ╲                ╱
                      ▼              ▼
                        FrameResult
### 一、Tensor层职责

**Tensor推理框架之间交换的数据格式**

统一抽象:
```cpp
ov::Tensor
TensorRT Buffer
Ort::Value
```

<font color="red">只负责：数据、shape、type、memory</font>

> **关键理解**：一个 `TensorFrame` 对应 **一帧**（如一个 NV12 帧），其中包含 **多个 `TensorPlane`**，每个 `TensorPlane` 对应帧中的一个 **数据块**（如 Y 平面、UV 平面）。

```cpp
/// 一个 Tensor = 一段数据（如一个 Y plane）
struct TensorPlane
{
    std::string name;                    // 如 "y"、"uv"、"output_0"
    TensorType type;                     // 如 UINT8、FP32
    TensorShape shape;                   // 如 {1, 416, 416, 1}
    std::shared_ptr<TensorBuffer> buffer;// 持有的内存区域
};

/// 一个 Package = 一帧的所有输入/输出 Tensor
struct TensorFrame
{
    // key = tensor name，如 "y" → Y plane, "uv" → UV plane
    std::unordered_map<std::string, std::unique_ptr<TensorPlane>> tensors_;
};
```

**示例**：NV12 帧 `416×416` 经预处理后打包为：

```
TensorFrame (一帧)
  ├── "y"  → TensorPlane { shape: {1, 416, 416, 1}, type: UINT8 }   ← Y 平面
  └── "uv" → TensorPlane { shape: {1, 208, 416, 2}, type: UINT8 }   ← UV 交错平面
```

多个 `TensorPlane` 可以**共享同一块 `IMediaBuffer` 的不同区域**（通过 `offset` 偏移），实现零拷贝。

### 二、Engine职责

Engine只负责：

```plaintext
Tensor
 ↓
推理
 ↓
Tensor
```
例如：

```cpp
class IInferenceEngine {
public:
    virtual bool Infer(const TensorFrame& input, TensorFrame& output)=0;
};
```

<font color="red">Engine只关心：模型文件、设备、推理请求、Tensor转换</font>


例如：
OpenVINO：

```plaintext
TensorFrame
 ↓
ov::Tensor
 ↓
InferRequest
 ↓
ov::Tensor
 ↓
TensorFrame
```

TensorRT：

```plaintext
TensorFrame
 ↓
DeviceBuffer
 ↓
enqueueV3
 ↓
TensorFrame
```

### 三、Model职责

AI语义层。
Model负责：预处理和后处理

```plaintext
VideoFrame
 ↓
TensorFrame
 
 ↓

TensorFrame
 ↓
FrameResult
```

例如：

YOLO Preprocess：
```plaintext
NV12
 ↓
Resize
 ↓
LetterBox
 ↓
Normalize
 ↓
NCHW
 ↓
TensorFrame
```
YOLO Postprocess：
```plaintext
Tensor
 ↓
Decode
 ↓
NMS
 ↓
FrameResult
```

```cpp
class IModel
{
public:

    virtual TensorFrame
    Preprocess(
        const VideoFrame&) = 0;

    virtual FrameResult
    Postprocess(
        const TensorFrame&) = 0;
};

class YoloModel
{
};

class OCRModel
{
};

class PoseModel
{
};
```
它们共享同一个：OpenVinoEngine

### 四、Session职责

Session负责：Model + Engine 组合。

Session内部：
```cpp
class InferenceSession
{
    IModel

    IInferenceEngine
};
```
执行：
```plaintext

frame
 ↓
model->Preprocess()
 ↓
engine->Infer()
 ↓
model->Postprocess()
 ↓
result
```
Session是流程编排者, 不做：推理、NMS、Resize, 只负责：调度

### 五、Result职责

Result是业务层。负责：检测框、关键点、掩码、分类结果

```cpp
struct ObjectResult
{
    int class_id;

    float score;

    Rect bbox;
};

struct FrameResult
{
    uint64_t frame_id;

    uint64_t pts;

    std::vector<ObjectResult> objects;
};
```

### 六、最终职责边界


| 子模块 | 职责 |
| :---: | :---: |
| Tensor | 数据交换 <br> raw -> tensor |
| Engine | 加载模型 推理（同步/异步） 推理请求池 |
| Model | 预处理 Frame -> Tensor <br> 后处理 Tensor -> Result |
| Session | 组织流程 |
| Result | 业务语义 |

**调用链：**
```plaintext
SourceNode
    ↓
VideoFrame
    ↓
YoloModel::Preprocess
    ↓
TensorFrame
    ↓
OpenVinoEngine::Infer
    ↓
TensorFrame
    ↓
YoloModel::Postprocess
    ↓
FrameResult
    ↓
OSDNode
    ↓
EncodeNode
    ↓
PushNode
```

## 流程

### 1. 总体数据流

```
MediaFrame (from decoder)
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│                 InferenceSession::Infer()                   │
│                                                             │
│  ┌─────────────────────┐     ┌─────────────────────┐       │
│  │  model->Preprocess  │ ──► │  engine->Infer      │       │
│  │  (Frame→TensorPkg)  │     │  (TensorPkg→TensorPkg)      │
│  └─────────────────────┘     └──────────┬──────────┘       │
│                                         ▼                   │
│                                 ┌─────────────────────┐     │
│                                 │ model->Postprocess  │     │
│                                 │ (TensorPkg→Result)  │     │
│                                 └─────────────────────┘     │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
FrameResult (to OSDNode)
```

### 2. 初始化流程

```
调用方 (Pipeline / Demo)
    │
    ├── 创建 IModel (如 YoloModel)
    │   └── model->Initialize(config)
    │       ├── 创建 OpenVinoYoloPreprocessor
    │       ├── 设置输入尺寸 (640x640 默认)
    │       └── 初始化完成
    │
    ├── 创建 IInferenceEngine (如 OpenVinoCpuEngine)
    │   ├── engine->SetPreprocessConfig(...)
    │   │   └── 设置 NV12/I420 输入、RGB/BGR 输出、缩放因子等
    │   └── engine->LoadModel(config)
    │       ├── core_.read_model(path)        ← 读取 IR/ONNX
    │       ├── 记录原始模型输入形状
    │       ├── ApplyOpenVinoPreprocess()     ← 注入 OpenVINO 内部预处理
    │       │   ├── 设置输入元素类型 u8
    │       │   ├── 设置颜色格式 (NV12_TWO_PLANES / I420_THREE_PLANES)
    │       │   ├── 设置颜色转换 (YUV→RGB/BGR)
    │       │   ├── 设置 resize (LINEAR)
    │       │   ├── 设置元素类型转换
    │       │   └── 设置归一化 scale
    │       ├── core_.compile_model(...)      ← 编译模型到 CPU
    │       └── request_pool_->Initialize()    ← 创建 N 个 InferRequest
    │
    └── session.Initialize(model, engine)
        ├── 保存 model_ / engine_
        ├── engine->GetInputShape()           ← 获取模型输入形状
        └── model->ConfigureInputShape()      ← 将形状回写给 model
            └── 解析 NCHW/NHWC → 更新 input_width_ / input_height_
                └── ResetPreprocessor()       ← 按真实输入尺寸重建 preprocessor
```

### 3. 同步推理流程 (Session::Infer)

```
Session::Infer(MediaFrame frame)
    │
    ├── [Step 1] model->Preprocess(frame)
    │   └── YoloModel::Preprocess
    │       └── preprocessor_->Process(frame)
    │           ├── OpenVinoYoloPreprocessor::Process
    │           ├── ValidateFrame()           ← 校验 buffer / 尺寸 / 对齐
    │           │
    │           ├── 如果是 NV12:
    │           │   ├── PackNv12(frame)
    │           │   │   ├── y: [1,H,W,1]  ← MediaFramePlaneTensorBuffer (零拷贝)
    │           │   │   └── uv: [1,H/2,W/2,2]
    │           │   └── → TensorFrame {"y", "uv"}
    │           │
    │           └── 如果是 I420:
    │               ├── PackI420(frame)
    │               │   ├── y: [1,H,W,1]
    │               │   ├── u: [1,H/2,W/2,1]
    │               │   └── v: [1,H/2,W/2,1]
    │               └── → TensorFrame {"y", "u", "v"}
    │
    │   ★ 零拷贝: 当 stride == row_bytes 时，TensorBuffer 直接引用 MediaFrame 的 buffer
    │   ★ 否则: 逐行 compact 拷贝到 CpuTensorBuffer，消除 padding
    │
    ├── [Step 2] engine->Infer(input_package, output_package)
    │   └── OpenVinoCpuEngine::Infer
    │       ├── request_pool_->Acquire()         ← 从池中获取空闲 InferRequest（可能阻塞）
    │       ├── RequestLease(request)            ← RAII，析构时自动 Release
    │       │
    │       ├── BindInputs(request, model, input)
    │       │   └── 遍历模型输入端口:
    │       │       ├── FindInputTensor()        ← 按名称 / "y"/"uv" / fallback 查找 TensorPlane
    │       │       ├── ToOvElementType()        ← TensorType → ov::element::Type
    │       │       ├── ToOvShape()              ← TensorShape → ov::Shape
    │       │       ├── ov::Tensor(type, shape, data)  ← 包装为 ov::Tensor（共享内存，零拷贝）
    │       │       └── request.set_tensor(port, tensor)
    │       │
    │       ├── request->infer()                 ← OpenVINO 执行推理
    │       │   └── OpenVINO 内部预处理流水线:
    │       │       ├── 颜色转换 (YUV→RGB/BGR)
    │       │       ├── Resize (LINEAR)
    │       │       ├── 元素类型转换 (u8→fp32)
    │       │       └── Scale (如 /255.0f)
    │       │
    │       └── output = CollectOutputs(request, model)
    │           └── 遍历模型输出端口:
    │               ├── request.get_tensor(port)      ← 获取 ov::Tensor
    │               ├── 创建 CpuTensorBuffer(bytes)
    │               ├── memcpy 拷贝到 CPU             ← 必须拷贝，析构后数据失效
    │               ├── 构造 TensorPlane (name/type/shape/buffer)
    │               └── → TensorFrame {output_0, ...}
    │
    └── [Step 3] model->Postprocess(output_package)
        └── YoloModel::Postprocess
            ├── FindFirstFloatTensor()               ← 查找第一个 FP32 输出 Tensor
            │
            ├── DecodeYoloTensor(tensor, conf_threshold, width, height)
            │   ├── ResolveYoloShape()               ← 解析 tensor shape:
            │   │   ├── 3D: [count, attrs, 1] 或 [1, attrs, count]
            │   │   ├── 2D: [count, attrs] 或 [attrs, count]
            │   │   └── 判断是否 transpose
            │   ├── 遍历 count 个候选框:
            │   │   ├── 读取 cx/cy/w/h
            │   │   ├── 读取 objectness (YOLOv5) 或无 objectness (YOLOv8)
            │   │   ├── 遍历 classes，找到最高分
            │   │   ├── score = objectness × class_score
            │   │   ├── 过滤 conf_threshold
            │   │   └── MakeRect() → Rectangle (坐标裁剪到图像内)
            │   └── → vector<DetectionCandidate>
            │
            ├── Nms(candidates, nms_threshold)
            │   ├── 按 score 降序排列
            │   ├── 按 class_id 分组 + IoU 过滤
            │   └── → vector<DetectionCandidate> (保留的)
            │
            └── 组装 FrameResult
                ├── frame_id = 0
                ├── pts = 0
                └── objects = vector<ObjectResult>
                    └── 每个包含 ObjectMeta {class_id, score, rect}
```

### 4. 异步推理流程

```
调用方
    │
    └── engine->InferAsync(ctx, input, callback)
        │
        ├── input_copy = make_shared<TensorFrame>(input)  ← 异步需要拷贝
        ├── request_pool_->Acquire()                        ← 获取请求
        ├── BindInputs(*request, model, *input_copy)        ← 绑定输入
        │
        ├── request->set_callback([=](exception_ptr ex) {
        │       ├── if (ex) rethrow;
        │       ├── output = CollectOutputs(*request, model)
        │       ├── callback(ctx, std::move(output))        ← 用户回调
        │       └── request_pool_->Release(request)         ← 释放回池
        │   })
        │
        └── request->start_async()                          ← 提交异步推理
```

### 5. Pipeline 集成流程

```
SourceNode (RTSP / File / Camera)
    │
    └── MediaFrame (NV12 / I420)
        │
        ▼
    InferenceNode (继承 INode + SinkNode<MediaFrame>)
        │
        ├── 创建 InferenceSession (生命周期内仅一次)
        │   ├── new YoloModel → model->Initialize(config)
        │   ├── new OpenVinoCpuEngine → engine->LoadModel(config)
        │   └── session.Initialize(model, engine)
        │
        └── 每帧回调:
            └── session.Infer(frame)
                ├── model->Preprocess  (Frame → YUV TensorFrame)
                ├── engine->Infer      (OpenVINO 推理)
                └── model->Postprocess (Decode + NMS → FrameResult)
                    │
                    ▼
                FrameResult → OSDNode → EncodeNode → PushNode
```

### 6. 关键设计要点

| 环节 | 设计要点 |
|:---:|:---|
| **零拷贝输入** | `MediaFramePlaneTensorBuffer` 直接引用帧 buffer，不拷贝；仅在 stride ≠ row_bytes 时才 compact 拷贝 |
| **零拷贝引擎** | `ov::Tensor(type, shape, data)` 共享上层 buffer 内存，引擎内部预处理时才真正转换 |
| **引擎内部预处理** | OpenVINO 的 `PrePostProcessor` 注入颜色转换 + resize + normalize 到模型图中，输出已经是归一化后的 FP32 |
| **推理请求池** | `OpenVinoInferRequestPool` 用 mutex + condition_variable 管理固定数量的 InferRequest，避免反复创建 |
| **RAII 管理** | `RequestLease` 确保 InferRequest 在作用域结束时自动释放回池，异常安全 |
| **YOLO 通用解析** | `DecodeYoloTensor` 同时支持 2D/3D、transpose/非 transpose、有无 objectness (v5/v8)，统一处理 |
| **跨框架扩展** | 替换 `IInferenceEngine` 实现即可接入 TensorRT / ONNX Runtime / TNN 等，Model/Result 层无需变更 |

### 7. 数据格式转换总览

```
MediaFrame (YUV420/NV12)
    │
    │  Model::Preprocess (OpenVinoYoloPreprocessor)
    │  ┌─────────────────────────────────────────────────┐
    │  │ y: [1, H,   W,   1]  UINT8                      │
    │  │ u: [1, H/2, W/2, 1]  UINT8  (I420)              │
    │  │ v: [1, H/2, W/2, 1]  UINT8  (I420)              │
    │  │ 或:                                               │
    │  │ y:  [1, H,   W,   1]  UINT8                      │
    │  │ uv: [1, H/2, W/2, 2]  UINT8  (NV12)              │
    │  └─────────────────────────────────────────────────┘
    │
    ▼
TensorFrame (raw YUV planes)
    │
    │  engine->Infer (OpenVINO 内部预处理)
    │  ┌─────────────────────────────────────────────────┐
    │  │ YUV → RGB/BGR                                   │
    │  │ Resize → model input size (640x640)             │
    │  │ UINT8 → FP32                                    │
    │  │ Scale (1/255)                                   │
    │  │ NCHW layout                                     │
    │  └─────────────────────────────────────────────────┘
    │
    ▼
TensorFrame (FP32 NCHW model output)
    │
    │  Model::Postprocess (Decode + NMS)
    │  ┌─────────────────────────────────────────────────┐
    │  │ [count, attrs] 或 [1, attrs, count] FP32        │
    │  │ → Decode → vector<DetectionCandidate>            │
    │  │ → NMS    → kept candidates                      │
    │  │ → FrameResult {objects: [{class_id, score, rect}]}│
    │  └─────────────────────────────────────────────────┘
    │
    ▼
FrameResult (业务语义)
```