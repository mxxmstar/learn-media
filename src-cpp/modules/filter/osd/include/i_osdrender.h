#pragma once

#include "defines/media_frame.hpp"
#include "osd_batch.h"

class IOSDRenderer {
public:
    virtual ~IOSDRenderer() = default;

    virtual bool Draw(MediaFrame& frame, const OverlayBatch& batch) = 0;
};
