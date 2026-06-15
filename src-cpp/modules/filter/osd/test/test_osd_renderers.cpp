#include "nv12_osdrender.h"
#include "yuv420_osdrender.h"

#include "defines/i_media_buffer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class TestBuffer : public IMediaBuffer {
public:
    explicit TestBuffer(size_t size)
        : data_(size)
    {}

    uint8_t* Data() override {
        return data_.data();
    }

    const uint8_t* Data() const override {
        return data_.data();
    }

    size_t Size() const override {
        return data_.size();
    }

private:
    std::vector<uint8_t> data_;
};

MediaFrame MakeNV12Frame(int width, int height, std::shared_ptr<TestBuffer>& buffer) {
    const size_t y_size = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uv_size = static_cast<size_t>(width) * static_cast<size_t>((height + 1) / 2);
    buffer = std::make_shared<TestBuffer>(y_size + uv_size);
    std::fill(buffer->Data(), buffer->Data() + y_size, 0);
    std::fill(buffer->Data() + y_size, buffer->Data() + y_size + uv_size, 128);

    MediaFrame frame;
    frame.pixel_format = PixelFormat::kNV12;
    frame.width = width;
    frame.height = height;
    frame.stride[0] = width;
    frame.stride[1] = width;
    frame.plane_offset[0] = 0;
    frame.plane_offset[1] = static_cast<int32_t>(y_size);
    frame.plane_count = 2;
    frame.buffer = buffer;
    return frame;
}

MediaFrame MakeI420Frame(int width, int height, std::shared_ptr<TestBuffer>& buffer) {
    const int chroma_width = (width + 1) / 2;
    const int chroma_height = (height + 1) / 2;
    const size_t y_size = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uv_size = static_cast<size_t>(chroma_width) * static_cast<size_t>(chroma_height);
    buffer = std::make_shared<TestBuffer>(y_size + uv_size * 2);
    std::fill(buffer->Data(), buffer->Data() + y_size, 0);
    std::fill(buffer->Data() + y_size, buffer->Data() + y_size + uv_size * 2, 128);

    MediaFrame frame;
    frame.pixel_format = PixelFormat::kI420;
    frame.width = width;
    frame.height = height;
    frame.stride[0] = width;
    frame.stride[1] = chroma_width;
    frame.stride[2] = chroma_width;
    frame.plane_offset[0] = 0;
    frame.plane_offset[1] = static_cast<int32_t>(y_size);
    frame.plane_offset[2] = static_cast<int32_t>(y_size + uv_size);
    frame.plane_count = 3;
    frame.buffer = buffer;
    return frame;
}

uint8_t YAt(const MediaFrame& frame, int x, int y) {
    return frame.buffer->Data()[static_cast<size_t>(frame.plane_offset[0])
        + static_cast<size_t>(y) * static_cast<size_t>(frame.stride[0])
        + static_cast<size_t>(x)];
}

uint8_t NV12UAt(const MediaFrame& frame, int x, int y) {
    const size_t uv_offset = static_cast<size_t>(frame.plane_offset[1])
        + static_cast<size_t>(y / 2) * static_cast<size_t>(frame.stride[1])
        + static_cast<size_t>((x / 2) * 2);
    return frame.buffer->Data()[uv_offset];
}

uint8_t NV12VAt(const MediaFrame& frame, int x, int y) {
    const size_t uv_offset = static_cast<size_t>(frame.plane_offset[1])
        + static_cast<size_t>(y / 2) * static_cast<size_t>(frame.stride[1])
        + static_cast<size_t>((x / 2) * 2 + 1);
    return frame.buffer->Data()[uv_offset];
}

uint8_t I420UAt(const MediaFrame& frame, int x, int y) {
    return frame.buffer->Data()[static_cast<size_t>(frame.plane_offset[1])
        + static_cast<size_t>(y / 2) * static_cast<size_t>(frame.stride[1])
        + static_cast<size_t>(x / 2)];
}

uint8_t I420VAt(const MediaFrame& frame, int x, int y) {
    return frame.buffer->Data()[static_cast<size_t>(frame.plane_offset[2])
        + static_cast<size_t>(y / 2) * static_cast<size_t>(frame.stride[2])
        + static_cast<size_t>(x / 2)];
}

void TestNV12Rect() {
    std::shared_ptr<TestBuffer> buffer;
    auto frame = MakeNV12Frame(8, 6, buffer);

    auto rect = std::make_shared<OverlayRect>();
    rect->x = 1;
    rect->y = 1;
    rect->width = 4;
    rect->height = 3;
    rect->thickness = 1;
    rect->color = OSDColor::Red;

    OverlayBatch batch;
    batch.Add(rect);

    NV12Renderer renderer;
    assert(renderer.Draw(frame, batch));
    assert(YAt(frame, 1, 1) == OSDColor::Red.y);
    assert(YAt(frame, 4, 3) == OSDColor::Red.y);
    assert(YAt(frame, 2, 2) == 0);
    assert(NV12UAt(frame, 1, 1) == OSDColor::Red.u);
    assert(NV12VAt(frame, 1, 1) == OSDColor::Red.v);
}

void TestNV12ClipsOutsideFrame() {
    std::shared_ptr<TestBuffer> buffer;
    auto frame = MakeNV12Frame(6, 4, buffer);

    auto rect = std::make_shared<OverlayRect>();
    rect->x = -2;
    rect->y = -1;
    rect->width = 4;
    rect->height = 3;
    rect->thickness = 1;
    rect->color = OSDColor::Green;

    OverlayBatch batch;
    batch.Add(rect);

    NV12Renderer renderer;
    assert(renderer.Draw(frame, batch));
    assert(YAt(frame, 1, 0) == OSDColor::Green.y);
    assert(YAt(frame, 5, 3) == 0);
}

void TestNV21ChromaOrder() {
    std::shared_ptr<TestBuffer> buffer;
    auto frame = MakeNV12Frame(4, 4, buffer);
    frame.pixel_format = PixelFormat::kNV21;

    auto line = std::make_shared<OverlayLine>();
    line->x1 = 0;
    line->y1 = 0;
    line->x2 = 0;
    line->y2 = 0;
    line->thickness = 1;
    line->color = OSDColor::Blue;

    OverlayBatch batch;
    batch.Add(line);

    NV12Renderer renderer;
    assert(renderer.Draw(frame, batch));
    assert(YAt(frame, 0, 0) == OSDColor::Blue.y);
    assert(NV12UAt(frame, 0, 0) == OSDColor::Blue.v);
    assert(NV12VAt(frame, 0, 0) == OSDColor::Blue.u);
}

void TestNV12Text() {
    std::shared_ptr<TestBuffer> buffer;
    auto frame = MakeNV12Frame(24, 24, buffer);

    auto text = std::make_shared<OverlayText>();
    text->x = 2;
    text->y = 2;
    text->text = "A";
    text->scale = 1;
    text->color = OSDColor::Red;
    text->draw_background = true;
    text->background_padding = 1;
    text->background_color = OSDColor::Black;

    OverlayBatch batch;
    batch.Add(text);

    NV12Renderer renderer;
    assert(renderer.Draw(frame, batch));
    assert(YAt(frame, 1, 1) == OSDColor::Black.y);
    assert(YAt(frame, 5, 4) == OSDColor::Red.y);
    assert(NV12UAt(frame, 5, 4) == OSDColor::Red.u);
    assert(NV12VAt(frame, 5, 4) == OSDColor::Red.v);
}

void TestI420Line() {
    std::shared_ptr<TestBuffer> buffer;
    auto frame = MakeI420Frame(8, 8, buffer);

    auto line = std::make_shared<OverlayLine>();
    line->x1 = 0;
    line->y1 = 0;
    line->x2 = 3;
    line->y2 = 3;
    line->thickness = 1;
    line->color = OSDColor::Blue;

    OverlayBatch batch;
    batch.Add(line);

    Yuv420Renderer renderer;
    assert(renderer.Draw(frame, batch));
    assert(YAt(frame, 0, 0) == OSDColor::Blue.y);
    assert(YAt(frame, 1, 1) == OSDColor::Blue.y);
    assert(YAt(frame, 3, 3) == OSDColor::Blue.y);
    assert(YAt(frame, 2, 1) == 0);
    assert(I420UAt(frame, 0, 0) == OSDColor::Blue.u);
    assert(I420VAt(frame, 0, 0) == OSDColor::Blue.v);
}

void TestI420TextScale() {
    std::shared_ptr<TestBuffer> buffer;
    auto frame = MakeI420Frame(32, 32, buffer);

    auto text = std::make_shared<OverlayText>();
    text->x = 0;
    text->y = 0;
    text->text = "A";
    text->scale = 2;
    text->color = OSDColor::Green;

    OverlayBatch batch;
    batch.Add(text);

    Yuv420Renderer renderer;
    assert(renderer.Draw(frame, batch));
    assert(YAt(frame, 6, 4) == OSDColor::Green.y);
    assert(YAt(frame, 7, 5) == OSDColor::Green.y);
    assert(I420UAt(frame, 6, 4) == OSDColor::Green.u);
    assert(I420VAt(frame, 6, 4) == OSDColor::Green.v);
}

void TestInvalidFormatAndEmptyBatch() {
    MediaFrame frame;
    frame.pixel_format = PixelFormat::kRGB24;
    frame.width = 2;
    frame.height = 2;
    auto buffer = std::make_shared<TestBuffer>(12);
    frame.buffer = buffer;

    NV12Renderer renderer;
    OverlayBatch empty;
    assert(renderer.Draw(frame, empty));

    OverlayBatch batch;
    batch.Add(std::make_shared<OverlayLine>());
    assert(!renderer.Draw(frame, batch));
}

int main() {
    TestNV12Rect();
    TestNV12ClipsOutsideFrame();
    TestNV21ChromaOrder();
    TestNV12Text();
    TestI420Line();
    TestI420TextScale();
    TestInvalidFormatAndEmptyBatch();
    return 0;
}
