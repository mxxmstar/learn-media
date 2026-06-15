#pragma once

#include "font_8x16.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace osd_detail {

constexpr int kFont8x16Width = 8;
constexpr int kFont8x16Height = 16;

struct TextBounds {
    int width = 0;
    int height = 0;
};

inline int NormalizeThickness(int thickness) {
    return std::max(1, thickness);
}

inline int NormalizeScale(int scale) {
    return std::max(1, scale);
}

template <typename DrawPixel>
void DrawFilledRect(DrawPixel&& draw_pixel, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            draw_pixel(x + xx, y + yy);
        }
    }
}

template <typename DrawPixel>
void DrawThickPoint(DrawPixel&& draw_pixel, int x, int y, int thickness) {
    thickness = NormalizeThickness(thickness);
    const int start = -(thickness / 2);
    DrawFilledRect(draw_pixel, x + start, y + start, thickness, thickness);
}

template <typename DrawPixel>
void DrawBresenhamLine(
    DrawPixel&& draw_pixel,
    int x1,
    int y1,
    int x2,
    int y2,
    int thickness
) {
    const int dx = std::abs(x2 - x1);
    const int sx = x1 < x2 ? 1 : -1;
    const int dy = -std::abs(y2 - y1);
    const int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        DrawThickPoint(draw_pixel, x1, y1, thickness);
        if (x1 == x2 && y1 == y2) {
            break;
        }

        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

inline TextBounds MeasureText8x16(
    const std::string& text,
    int scale,
    int char_spacing,
    int line_spacing
) {
    if (text.empty()) {
        return {};
    }

    scale = NormalizeScale(scale);
    char_spacing = std::max(0, char_spacing);
    line_spacing = std::max(0, line_spacing);

    const int glyph_width = kFont8x16Width * scale;
    const int glyph_height = kFont8x16Height * scale;
    int max_width = 0;
    int line_width = 0;
    int lines = 1;
    bool line_has_chars = false;

    auto append_glyph = [&]() {
        if (line_has_chars) {
            line_width += char_spacing;
        }
        line_width += glyph_width;
        line_has_chars = true;
    };

    for (const auto ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            max_width = std::max(max_width, line_width);
            line_width = 0;
            line_has_chars = false;
            ++lines;
            continue;
        }

        const int repeat = ch == '\t' ? 4 : 1;
        for (int i = 0; i < repeat; ++i) {
            append_glyph();
        }
    }

    max_width = std::max(max_width, line_width);
    return {max_width, lines * glyph_height + (lines - 1) * line_spacing};
}

template <typename DrawPixel>
void DrawGlyph8x16(DrawPixel&& draw_pixel, unsigned char ch, int x, int y, int scale) {
    scale = NormalizeScale(scale);

    for (int row = 0; row < kFont8x16Height; ++row) {
        const auto bits = font_8x16[ch][row];
        for (int col = 0; col < kFont8x16Width; ++col) {
            if ((bits & (0x80 >> col)) == 0) {
                continue;
            }

            DrawFilledRect(
                draw_pixel,
                x + col * scale,
                y + row * scale,
                scale,
                scale
            );
        }
    }
}

template <typename DrawPixel>
void DrawText8x16(
    DrawPixel&& draw_pixel,
    const std::string& text,
    int x,
    int y,
    int scale,
    int char_spacing,
    int line_spacing
) {
    if (text.empty()) {
        return;
    }

    scale = NormalizeScale(scale);
    char_spacing = std::max(0, char_spacing);
    line_spacing = std::max(0, line_spacing);

    const int glyph_width = kFont8x16Width * scale;
    const int line_height = kFont8x16Height * scale + line_spacing;
    int cursor_x = x;
    int cursor_y = y;
    bool line_has_chars = false;

    auto draw_char = [&](unsigned char ch) {
        if (line_has_chars) {
            cursor_x += char_spacing;
        }
        DrawGlyph8x16(draw_pixel, ch, cursor_x, cursor_y, scale);
        cursor_x += glyph_width;
        line_has_chars = true;
    };

    for (const auto ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += line_height;
            line_has_chars = false;
            continue;
        }
        if (ch == '\t') {
            for (int i = 0; i < 4; ++i) {
                draw_char(' ');
            }
            continue;
        }

        draw_char(static_cast<unsigned char>(ch));
    }
}

}  // namespace osd_detail
