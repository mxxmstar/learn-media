#pragma once

#include "defines/media_frame.hpp"
#include "render/render_config.h"

namespace render {

class IVideoRenderer {
public:
    virtual ~IVideoRenderer() = default;

    virtual bool Init(const RenderConfig& config) = 0;
    virtual bool Render(const MediaFrame& frame) = 0;
    virtual void PollEvents() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void Shutdown() = 0;
};

} // namespace render
