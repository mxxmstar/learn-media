#pragma once

#include <string>

namespace render {

struct RenderConfig {
    int window_width{1280};
    int window_height{720};
    std::string title{"learn-media renderer"};
    bool visible{true};
    bool vsync{true};
    bool close_on_escape{true};
};

} // namespace render
