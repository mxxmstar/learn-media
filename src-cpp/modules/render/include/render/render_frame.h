#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace render {

struct RgbaFrame {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> pixels;

    void Reset(int new_width, int new_height) {
        width = new_width;
        height = new_height;
        pixels.assign(static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height) * 4,
                      0);
    }

    bool Empty() const {
        return width <= 0 || height <= 0 || pixels.empty();
    }
};

} // namespace render
