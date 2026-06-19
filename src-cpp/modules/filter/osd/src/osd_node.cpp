#include "osd_node.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

#include "common/log/logmanager.h"
#include "nv12_osdrender.h"
#include "osd_batch.h"
#include "osd_color.h"
#include "yuv420_osdrender.h"

namespace pipeline {
namespace {

YuvColor ColorForClass(int class_id) {
    switch ((class_id % 4 + 4) % 4) {
        case 0:
            return OSDColor::Red;
        case 1:
            return OSDColor::Green;
        case 2:
            return OSDColor::Blue;
        default:
            return OSDColor::White;
    }
}

std::string FormatObjectLabel(const ObjectMeta& object) {
    std::ostringstream oss;
    oss << "c" << object.class_id << " "
        << std::fixed << std::setprecision(2) << object.score;
    return oss.str();
}

OverlayBatch BuildOverlayBatch(const FrameResult& result, const MediaFrame& frame) {
    OverlayBatch batch;
    if (frame.width <= 0 || frame.height <= 0) {
        return batch;
    }

    const int thickness = std::max(2, std::min(frame.width, frame.height) / 320);
    for (const auto& object_result : result.objects) {
        const auto& object = object_result.object;
        const auto& rect = object.rect;
        const int x1 = std::clamp(static_cast<int>(std::floor(rect.x)), 0, frame.width - 1);
        const int y1 = std::clamp(static_cast<int>(std::floor(rect.y)), 0, frame.height - 1);
        const int x2 = std::clamp(static_cast<int>(std::ceil(rect.x + rect.width)), 0, frame.width);
        const int y2 = std::clamp(static_cast<int>(std::ceil(rect.y + rect.height)), 0, frame.height);
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        const auto color = ColorForClass(object.class_id);
        auto overlay = std::make_shared<OverlayRect>();
        overlay->x = x1;
        overlay->y = y1;
        overlay->width = x2 - x1;
        overlay->height = y2 - y1;
        overlay->thickness = thickness;
        overlay->color = color;
        batch.Add(std::move(overlay));

        auto label = std::make_shared<OverlayText>();
        label->text = FormatObjectLabel(object);
        label->scale = 1;
        label->char_spacing = 1;
        label->color = color;
        label->draw_background = true;
        label->background_padding = 2;
        label->background_color = OSDColor::Black;
        label->x = x1;

        const int text_height = 16 * label->scale;
        const int label_height = text_height + label->background_padding * 2;
        const int preferred_y = y1 >= label_height ? y1 - label_height : y1 + thickness;
        label->y = std::clamp(preferred_y, 0, std::max(0, frame.height - text_height));
        batch.Add(std::move(label));
    }

    return batch;
}

void ClearBackend(FrameMessage& frame) {
    if (!frame) {
        return;
    }
    frame->backend.type = BackendHandle::NONE;
    frame->backend.ptr = nullptr;
}

} // namespace

OSDNode::OSDNode(std::shared_ptr<PipelineState> state)
    : state_(std::move(state)) {}

bool OSDNode::Init() {
    return true;
}

bool OSDNode::Start() {
    return true;
}

void OSDNode::Stop() {}

void OSDNode::Deinit() {}

std::string OSDNode::Name() const {
    return "osd";
}

void OSDNode::Process(InferenceMessagePtr message) {
    if (!message || !message->frame) {
        return;
    }

    auto frame = std::move(message->frame);
    auto batch = BuildOverlayBatch(message->result, *frame);
    if (!batch.Empty()) {
        bool drawn = false;
        if (frame->pixel_format == PixelFormat::kI420) {
            Yuv420Renderer renderer;
            drawn = renderer.Draw(*frame, batch);
        } else if (frame->pixel_format == PixelFormat::kNV12 ||
                   frame->pixel_format == PixelFormat::kNV21) {
            NV12Renderer renderer;
            drawn = renderer.Draw(*frame, batch);
        }

        if (!drawn) {
            state_->stats.osd_errors.fetch_add(1);
            LOG_MAIN_ERROR_AT("draw OSD failed");
        } else {
            ClearBackend(frame);
        }
    }

    state_->stats.osd_frames.fetch_add(1);
    Emit(std::move(frame));
}

} // namespace pipeline
