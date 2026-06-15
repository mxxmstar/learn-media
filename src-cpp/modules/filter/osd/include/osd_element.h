#pragma once
#include "osd_color.h"
#include <string>

enum class OverlayType {
    Rect,
    Line,
    Text
};

class OverlayElement {
public:
    virtual ~OverlayElement() = default;

public:    
    OverlayType type;
    YuvColor color;
    int thickness = 2;
    
};

class OverlayRect : public OverlayElement {
public:    
    OverlayRect() {
        type = OverlayType::Rect;
    }

public:    
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class OverlayLine : public OverlayElement {
public:
    OverlayLine() {
        type = OverlayType::Line;
    }

public:
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
};

class OverlayText : public OverlayElement {
public:
    OverlayText() {
        type = OverlayType::Text;
    }

public:
    int x = 0;
    int y = 0;
    int scale = 1;
    int char_spacing = 0;
    int line_spacing = 0;
    bool draw_background = false;
    int background_padding = 1;
    YuvColor background_color = OSDColor::Black;
    std::string text;
};
