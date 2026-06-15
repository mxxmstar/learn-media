#include "yuv420_osdrender.h"

#include "osd_geometry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace {

struct Planar420View {
    uint8_t* y = nullptr;
    uint8_t* u = nullptr;
    uint8_t* v = nullptr;
    int width = 0;
    int height = 0;
    int y_stride = 0;
    int u_stride = 0;
    int v_stride = 0;
};

size_t PlaneEnd(size_t offset, int stride, int rows, int visible_bytes) {
    return offset + static_cast<size_t>(stride) * static_cast<size_t>(rows - 1)
        + static_cast<size_t>(visible_bytes);
}

bool BuildPlanar420View(MediaFrame& frame, Planar420View& view) {
    if (frame.pixel_format != PixelFormat::kI420) {
        return false;
    }
    if (!frame.buffer || !frame.buffer->Data() || frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    const int width = frame.width;
    const int height = frame.height;
    const int chroma_width = (width + 1) / 2;
    const int chroma_height = (height + 1) / 2;
    const int y_stride = frame.stride[0] > 0 ? frame.stride[0] : width;
    const int u_stride = frame.stride[1] > 0 ? frame.stride[1] : chroma_width;
    const int v_stride = frame.stride[2] > 0 ? frame.stride[2] : chroma_width;

    if (y_stride < width || u_stride < chroma_width || v_stride < chroma_width) {
        return false;
    }

    const size_t y_offset = frame.plane_offset[0] > 0
        ? static_cast<size_t>(frame.plane_offset[0])
        : 0;
    const size_t u_offset = frame.plane_offset[1] > 0
        ? static_cast<size_t>(frame.plane_offset[1])
        : y_offset + static_cast<size_t>(y_stride) * static_cast<size_t>(height);
    const size_t v_offset = frame.plane_offset[2] > 0
        ? static_cast<size_t>(frame.plane_offset[2])
        : u_offset + static_cast<size_t>(u_stride) * static_cast<size_t>(chroma_height);

    const size_t y_end = PlaneEnd(y_offset, y_stride, height, width);
    const size_t u_end = PlaneEnd(u_offset, u_stride, chroma_height, chroma_width);
    const size_t v_end = PlaneEnd(v_offset, v_stride, chroma_height, chroma_width);
    const size_t required = std::max({y_end, u_end, v_end});
    if (frame.buffer->Size() < required) {
        return false;
    }

    auto* data = frame.buffer->Data();
    view.y = data + y_offset;
    view.u = data + u_offset;
    view.v = data + v_offset;
    view.width = width;
    view.height = height;
    view.y_stride = y_stride;
    view.u_stride = u_stride;
    view.v_stride = v_stride;
    return true;
}

}  // namespace

bool Yuv420Renderer::Draw(MediaFrame& frame, const OverlayBatch& batch) {
    if (batch.Empty()) {
        return true;
    }

    Planar420View view;
    if (!BuildPlanar420View(frame, view)) {
        return false;
    }

    for (const auto& item : batch.Items()) {
        if (item) {
            DrawElement(frame, *item);
        }
    }
    return true;
}

void Yuv420Renderer::DrawElement(MediaFrame& frame, const OverlayElement& item) {
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

void Yuv420Renderer::DrawRect(MediaFrame& frame, const OverlayRect& rect) {
    if (rect.width <= 0 || rect.height <= 0 || rect.thickness <= 0) {
        return;
    }

    const int thickness = std::min({rect.thickness, rect.width, rect.height});
    DrawHorizontal(frame, rect.x, rect.y, rect.width, thickness, rect.color);
    DrawHorizontal(frame, rect.x, rect.y + rect.height - thickness, rect.width, thickness, rect.color);
    DrawVertical(frame, rect.x, rect.y, rect.height, thickness, rect.color);
    DrawVertical(frame, rect.x + rect.width - thickness, rect.y, rect.height, thickness, rect.color);
}

void Yuv420Renderer::DrawLine(MediaFrame& frame, const OverlayLine& line) {
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

void Yuv420Renderer::DrawText(MediaFrame& frame, const OverlayText& text) {
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

void Yuv420Renderer::DrawHorizontal(
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

void Yuv420Renderer::DrawVertical(
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

void Yuv420Renderer::drawPixelYUV(MediaFrame& frame, int x, int y, YuvColor c) {
    Planar420View view;
    if (!BuildPlanar420View(frame, view)) {
        return;
    }
    if (x < 0 || y < 0 || x >= view.width || y >= view.height) {
        return;
    }

    view.y[static_cast<size_t>(y) * static_cast<size_t>(view.y_stride) + static_cast<size_t>(x)] = c.y;

    const int chroma_x = x / 2;
    const int chroma_y = y / 2;
    view.u[static_cast<size_t>(chroma_y) * static_cast<size_t>(view.u_stride)
        + static_cast<size_t>(chroma_x)] = c.u;
    view.v[static_cast<size_t>(chroma_y) * static_cast<size_t>(view.v_stride)
        + static_cast<size_t>(chroma_x)] = c.v;
}
