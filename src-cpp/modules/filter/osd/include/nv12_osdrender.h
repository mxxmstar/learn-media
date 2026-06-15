#pragma once
#include "i_osdrender.h"
class NV12Renderer : public IOSDRenderer {
public:
    bool Draw(MediaFrame& frame, const OverlayBatch& batch) override;
    void DrawElement(MediaFrame& frame, const OverlayElement& item);
    void DrawRect(MediaFrame& frame, const OverlayRect& rect);
    void DrawLine(MediaFrame& frame, const OverlayLine& line);
    void DrawText(MediaFrame& frame, const OverlayText& text);

    void DrawHorizontal(MediaFrame& frame, int x, int y, int len, int thickness, YuvColor c);
    void DrawVertical(MediaFrame& frame, int x, int y, int len, int thickness, YuvColor c);

private:    
    void drawPixelYUV(MediaFrame& frame, int x, int y, YuvColor c);
};
