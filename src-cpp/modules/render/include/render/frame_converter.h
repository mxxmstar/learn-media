#pragma once

#include <string>

#include "defines/media_frame.hpp"
#include "render/render_frame.h"

namespace render {

class FrameConverter {
public:
    bool ConvertToRgba(const MediaFrame& frame, RgbaFrame& out, std::string* error = nullptr) const;
};

} // namespace render
