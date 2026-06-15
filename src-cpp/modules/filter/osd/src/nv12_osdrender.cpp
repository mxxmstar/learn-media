#include "nv12_osdrender.h"

#include "osd_geometry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace {

struct SemiPlanarView {
    uint8_t* y = nullptr;      // Y 平面起始指针
    uint8_t* uv = nullptr;     // UV 交错平面起始指针
    int width = 0;             // 图像宽度（像素）
    int height = 0;            // 图像高度（像素）
    int y_stride = 0;          // Y 平面行跨度（含 padding）
    int uv_stride = 0;         // UV 平面行跨度（含 padding）
    bool is_nv21 = false;      // 是否为 NV21（UV 顺序颠倒）
};

/// @brief 计算一个 plane 在 buffer 中所需的结束位置
size_t PlaneEnd(size_t offset, int stride, int rows, int visible_bytes) {
    return offset + static_cast<size_t>(stride) * static_cast<size_t>(rows - 1)
        + static_cast<size_t>(visible_bytes);
}

bool BuildSemiPlanarView(MediaFrame& frame, SemiPlanarView& view) {
    if (frame.pixel_format != PixelFormat::kNV12 && frame.pixel_format != PixelFormat::kNV21) {
        return false;
    }
    if (!frame.buffer || !frame.buffer->Data() || frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    const int width = frame.width;
    const int height = frame.height;
    const int y_stride = frame.stride[0] > 0 ? frame.stride[0] : width;
    const int uv_width_bytes = ((width + 1) / 2) * 2; // NV12 UV 平面每行字节数（对齐到偶数）
    const int uv_stride = frame.stride[1] > 0 ? frame.stride[1] : uv_width_bytes;
    const int uv_height = (height + 1) / 2; // UV 平面行数（高度的一半）

    if (y_stride < width || uv_stride < uv_width_bytes) {
        return false;
    }

    const size_t y_offset = frame.plane_offset[0] > 0
        ? static_cast<size_t>(frame.plane_offset[0])
        : 0;
    const size_t uv_offset = frame.plane_offset[1] > 0
        ? static_cast<size_t>(frame.plane_offset[1])
        : y_offset + static_cast<size_t>(y_stride) * static_cast<size_t>(height);

    const size_t y_end = PlaneEnd(y_offset, y_stride, height, width);
    const size_t uv_end = PlaneEnd(uv_offset, uv_stride, uv_height, uv_width_bytes);
    const size_t required = std::max(y_end, uv_end);
    if (frame.buffer->Size() < required) {
        return false;
    }

    auto* data = frame.buffer->Data();
    view.y = data + y_offset;
    view.uv = data + uv_offset;
    view.width = width;
    view.height = height;
    view.y_stride = y_stride;
    view.uv_stride = uv_stride;
    view.is_nv21 = frame.pixel_format == PixelFormat::kNV21;
    return true;
}

}  // namespace

bool NV12Renderer::Draw(MediaFrame& frame, const OverlayBatch& batch) {
    if (batch.Empty()) {
        return true;
    }

    SemiPlanarView view;
    if (!BuildSemiPlanarView(frame, view)) {
        return false;
    }

    for (const auto& item : batch.Items()) {
        if (item) {
            DrawElement(frame, *item);
        }
    }
    return true;
}

void NV12Renderer::DrawElement(MediaFrame& frame, const OverlayElement& item) {
    switch (item.type) {
        case OverlayType::Rect:
            if (const auto* rect = dynamic_cast<const OverlayRect*>(&item)) {
                DrawRect(frame, *rect);
            }
            break;
        case OverlayType::Line:
            if (const auto* line = dynamic_cast<const OverlayLine*>(&item)) {
                DrawLine(frame, *line);
            }
            break;
        case OverlayType::Text:
            if (const auto* text = dynamic_cast<const OverlayText*>(&item)) {
                DrawText(frame, *text);
            }
            break;
    }
}

void NV12Renderer::DrawRect(MediaFrame& frame, const OverlayRect& rect) {
    if (rect.width <= 0 || rect.height <= 0 || rect.thickness <= 0) {
        return;
    }

    const int thickness = std::min({rect.thickness, rect.width, rect.height});
    DrawHorizontal(frame, rect.x, rect.y, rect.width, thickness, rect.color);
    DrawHorizontal(frame, rect.x, rect.y + rect.height - thickness, rect.width, thickness, rect.color);
    DrawVertical(frame, rect.x, rect.y, rect.height, thickness, rect.color);
    DrawVertical(frame, rect.x + rect.width - thickness, rect.y, rect.height, thickness, rect.color);
}

void NV12Renderer::DrawLine(MediaFrame& frame, const OverlayLine& line) {
    if (line.thickness <= 0) {
        return;
    }

    osd_detail::DrawBresenhamLine(
        [this, &frame, c = line.color](int x, int y) {
            drawPixelYUV(frame, x, y, c);
        },
        line.x1,
        line.y1,
        line.x2,
        line.y2,
        line.thickness
    );
}

void NV12Renderer::DrawText(MediaFrame& frame, const OverlayText& text) {
    if (text.text.empty()) {
        return;
    }

    const int scale = osd_detail::NormalizeScale(text.scale);
    const int char_spacing = std::max(0, text.char_spacing);
    const int line_spacing = std::max(0, text.line_spacing);

    if (text.draw_background) {
        const auto bounds = osd_detail::MeasureText8x16(
            text.text,
            scale,
            char_spacing,
            line_spacing
        );
        const int padding = std::max(0, text.background_padding);
        osd_detail::DrawFilledRect(
            [this, &frame, c = text.background_color](int x, int y) {
                drawPixelYUV(frame, x, y, c);
            },
            text.x - padding,
            text.y - padding,
            bounds.width + padding * 2,
            bounds.height + padding * 2
        );
    }

    osd_detail::DrawText8x16(
        [this, &frame, c = text.color](int x, int y) {
            drawPixelYUV(frame, x, y, c);
        },
        text.text,
        text.x,
        text.y,
        scale,
        char_spacing,
        line_spacing
    );
}

void NV12Renderer::DrawHorizontal(
    MediaFrame& frame,
    int x,
    int y,
    int len,
    int thickness,
    YuvColor c
) {
    if (len <= 0 || thickness <= 0) {
        return;
    }

    for (int yy = 0; yy < thickness; ++yy) {
        for (int xx = 0; xx < len; ++xx) {
            drawPixelYUV(frame, x + xx, y + yy, c);
        }
    }
}

void NV12Renderer::DrawVertical(
    MediaFrame& frame,
    int x,
    int y,
    int len,
    int thickness,
    YuvColor c
) {
    if (len <= 0 || thickness <= 0) {
        return;
    }

    for (int yy = 0; yy < len; ++yy) {
        for (int xx = 0; xx < thickness; ++xx) {
            drawPixelYUV(frame, x + xx, y + yy, c);
        }
    }
}

void NV12Renderer::drawPixelYUV(MediaFrame& frame, int x, int y, YuvColor c) {
    SemiPlanarView view;
    if (!BuildSemiPlanarView(frame, view)) {
        return;
    }
    if (x < 0 || y < 0 || x >= view.width || y >= view.height) {
        return;
    }

    view.y[static_cast<size_t>(y) * static_cast<size_t>(view.y_stride) + static_cast<size_t>(x)] = c.y;

    const int uv_x = (x / 2) * 2;
    const int uv_y = y / 2;
    auto* uv = view.uv
        + static_cast<size_t>(uv_y) * static_cast<size_t>(view.uv_stride)
        + static_cast<size_t>(uv_x);
    if (view.is_nv21) {
        uv[0] = c.v;
        uv[1] = c.u;
    } else {
        uv[0] = c.u;
        uv[1] = c.v;
    }
}
