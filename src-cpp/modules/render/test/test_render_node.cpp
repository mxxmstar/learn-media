#include "render/render_node.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

class VectorMediaBuffer : public IMediaBuffer {
public:
    explicit VectorMediaBuffer(std::vector<std::uint8_t> data)
        : data_(std::move(data)) {}

    std::uint8_t* Data() override {
        return data_.data();
    }

    const std::uint8_t* Data() const override {
        return data_.data();
    }

    std::size_t Size() const override {
        return data_.size();
    }

private:
    std::vector<std::uint8_t> data_;
};

struct FakeRendererState {
    int init_count{0};
    int render_count{0};
    int poll_count{0};
    int shutdown_count{0};
};

class FakeRenderer : public render::IVideoRenderer {
public:
    explicit FakeRenderer(std::shared_ptr<FakeRendererState> state)
        : state_(std::move(state)) {}

    bool Init(const render::RenderConfig&) override {
        ++state_->init_count;
        return true;
    }

    bool Render(const MediaFrame& frame) override {
        ++state_->render_count;
        return frame.width == 2 && frame.height == 1 && frame.pixel_format == PixelFormat::kRGB24;
    }

    void PollEvents() override {
        ++state_->poll_count;
    }

    bool ShouldClose() const override {
        return false;
    }

    void Shutdown() override {
        ++state_->shutdown_count;
    }

private:
    std::shared_ptr<FakeRendererState> state_;
};

std::shared_ptr<MediaFrame> MakeFrame() {
    auto frame = std::make_shared<MediaFrame>();
    frame->type = MediaType::VIDEO;
    frame->width = 2;
    frame->height = 1;
    frame->pixel_format = PixelFormat::kRGB24;
    frame->buffer = std::make_shared<VectorMediaBuffer>(
        std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6});
    return frame;
}

} // namespace

int main() {
    auto fake_state = std::make_shared<FakeRendererState>();

    pipeline::RenderNode node(render::RenderConfig{},
                              std::shared_ptr<pipeline::PipelineState>{},
                              std::make_unique<FakeRenderer>(fake_state));

    assert(node.Init());
    assert(node.Start());
    node.Input().Receive(MakeFrame());
    node.Stop();
    node.Deinit();

    assert(fake_state->init_count == 1);
    assert(fake_state->render_count == 1);
    assert(fake_state->poll_count == 1);
    assert(fake_state->shutdown_count == 1);
    return 0;
}
