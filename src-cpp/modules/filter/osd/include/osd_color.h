#pragma once
#include <cstdint>

struct YuvColor {
    uint8_t y = 255;
    uint8_t u = 128;
    uint8_t v = 128;

    constexpr YuvColor() = default;

    constexpr YuvColor(uint8_t y_, uint8_t u_, uint8_t v_)
        : y(y_), u(u_), v(v_)
    {}
};

namespace OSDColor {
constexpr YuvColor Black{
    16,
    128,
    128
};

constexpr YuvColor White{
    255,
    128,
    128
};

constexpr YuvColor Red{
    76,
    84,
    255
};

constexpr YuvColor Green{
    149,
    43,
    21
};

constexpr YuvColor Blue{
    29,
    255,
    107
};

}   // namespace OSDColor
