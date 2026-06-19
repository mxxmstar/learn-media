# OSD（On-Screen Display）模块

## 概述

OSD 模块提供在 YUV 格式视频帧上叠加图形（矩形、线段、文字）的能力。支持 NV12/NV21（半平面）和 I420（全平面）两种主流 4:2:0 像素格式。

## 目录结构

```
osd/
├── include/
│   ├── i_osdrender.h      # 渲染器抽象接口
│   ├── osd_color.h         # YUV 颜色定义 & 预置色板
│   ├── osd_element.h       # 叠加元素类型（矩形/线段/文字）
│   ├── osd_batch.h         # 批量提交容器
│   ├── font_8x16.h         # 8×16 点阵字库（ASCII 256 字符）
│   ├── nv12_osdrender.h    # NV12/NV21 渲染器
│   └── yuv420_osdrender.h  # I420 渲染器
├── src/
│   ├── osd_geometry.h      # 几何绘制算法（模板头文件）
│   ├── nv12_osdrender.cpp  # NV12/NV21 实现
│   └── yuv420_osdrender.cpp# I420 实现
├── test/
│   ├── CMakeLists.txt
│   └── test_osd_renderers.cpp
└── CMakeLists.txt
```

## 核心接口

### 渲染器抽象 —— `IOSDRenderer`

```cpp
class IOSDRenderer {
public:
    virtual ~IOSDRenderer() = default;
    virtual bool Draw(MediaFrame& frame, const OverlayBatch& batch) = 0;
};
```

单一入口 `Draw()` 接受一个媒体帧和一批覆盖元素，将元素依次绘制到帧上。

### 元素类型 —— `osd_element.h`

| 类型 | 类 | 属性 |
|------|----|------|
| 矩形 | `OverlayRect` | `x, y, width, height` |
| 线段 | `OverlayLine` | `x1, y1, x2, y2` |
| 文字 | `OverlayText` | `x, y, scale, char_spacing, line_spacing, draw_background, background_padding, background_color, text` |

所有元素共享 `OverlayElement` 基类属性：`type`（枚举）、`color`（`YuvColor`）、`thickness`（默认 2）。

### 颜色 —— `osd_color.h`

- `YuvColor` 结构体：`{ y, u, v }`
- 预置色板（`OSDColor` 命名空间）：`Black`、`White`、`Red`、`Green`、`Blue`

### 批量提交 —— `osd_batch.h`

`OverlayBatch` 持有 `vector<shared_ptr<OverlayElement>>`，提供 `Add()`、`Clear()`、`Empty()`、`Items()` 方法。

## 渲染器实现

### NV12Renderer（`nv12_osdrender.cpp`）

- **支持格式**：NV12（U 在前、V 在后交错）和 NV21（V 在前、U 在后交错）
- 内部通过 `SemiPlanarView` 结构体描述 Y 平面和 UV 交错平面的指针、跨距、尺寸
- `BuildSemiPlanarView()` 从 `MediaFrame` 解析出各平面指针，并做边界校验
- 像素写入：Y 平面直接写入对应坐标；UV 平面按 2×2 块共享色度，`(x/2)*2` 对齐写入 UV 对，NV21 时交换 U/V 值

### Yuv420Renderer（`yuv420_osdrender.cpp`）

- **支持格式**：I420（Y、U、V 三个独立平面）
- 内部通过 `Planar420View` 结构体描述三个平面的指针、跨距、尺寸
- `BuildPlanar420View()` 从 `MediaFrame` 解析出三个平面指针，并做边界校验
- 像素写入：Y 直接写入；U、V 按 `(x/2, y/2)` 分别写入对应平面

### 通用绘制流程

1. `Draw()` 校验 `OverlayBatch` 非空，构建平面视图，遍历所有元素调用 `DrawElement()`
2. `DrawElement()` 根据 `OverlayType` 动态转型并分发到 `DrawRect()` / `DrawLine()` / `DrawText()`
3. `DrawRect()`：画四条边（顶部/底部水平条 + 左右垂直条），thickness 不超过 rect 宽/高
4. `DrawLine()`：委托 `osd_detail::DrawBresenhamLine()` 模板，Bresenham 算法 + 粗点绘制
5. `DrawText()`：支持可选背景矩形；委托 `osd_detail::DrawText8x16()` 模板绘制文字

## 几何绘制算法（`osd_geometry.h`，模板头文件）

所有绘制操作通过模板函数泛化，由调用方传入 lambda 作为像素写入器：

| 函数 | 描述 |
|------|------|
| `DrawFilledRect()` | 填充矩形 |
| `DrawThickPoint()` | 绘制指定粗细的方块点 |
| `DrawBresenhamLine()` | Bresenham 直线算法，支持线宽 |
| `MeasureText8x16()` | 计算文字在 8×16 字体下的宽高 |
| `DrawGlyph8x16()` | 绘制单个 8×16 字符（支持缩放） |
| `DrawText8x16()` | 绘制完整字符串（支持 `\n` 换行、`\t` 制表符、字符间距、行间距） |

### 8×16 点阵字库（`font_8x16.h`）

- 256 个 ASCII 字符，每个字符 16 字节（8×16 像素，每字节表示一行）
- 来自 Linux 内核源码（GPL-2.0 许可）

## 边界安全

- 所有平面写入前检查 `x, y` 是否在 `[0, width/height)` 范围内
- 从 `MediaFrame` 解析时校验：
  - 像素格式匹配
  - `buffer` / `Data()` 非空
  - `width/height > 0`
  - `stride >= 可见宽度`
  - buffer 大小足够容纳所有平面
- 无效格式或参数时 `Draw()` 返回 `false`

## 测试（`test_osd_renderers.cpp`）

| 测试用例 | 描述 |
|-----------|------|
| `TestNV12Rect` | NV12 上绘制矩形，验证角点内外像素 |
| `TestNV12ClipsOutsideFrame` | 矩形超出画面边界时的裁剪行为 |
| `TestNV21ChromaOrder` | NV21 格式下 UV 顺序交换 |
| `TestNV12Text` | NV12 上绘制带背景的文字 |
| `TestI420Line` | I420 上绘制对角线 |
| `TestI420TextScale` | I420 上绘制缩放文字（2 倍） |
| `TestInvalidFormatAndEmptyBatch` | 空 batch 返回 true，不合法格式返回 false |
