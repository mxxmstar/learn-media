#pragma once
#include <vector>
#include <memory>
#include <utility>
#include "osd_element.h"
#include "osd_color.h"

class OverlayBatch {
public:
    void Add(std::shared_ptr<OverlayElement> item) {
        items_.push_back(
            std::move(item)
        );
    }

    void Clear() {
        items_.clear();
    }
    bool Empty() const {
        return items_.empty();
    }

    const auto& Items() const {
        return items_;
    }

private:
    std::vector<std::shared_ptr<OverlayElement>> items_;
};
