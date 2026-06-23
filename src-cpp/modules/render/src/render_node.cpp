#include "render/render_node.h"

#include <iostream>
#include <utility>

#include "render/opengl_video_renderer.h"

namespace pipeline {

RenderNode::RenderNode(std::shared_ptr<PipelineState> state)
    : RenderNode(render::RenderConfig{}, std::move(state)) {}

RenderNode::RenderNode(render::RenderConfig config, std::shared_ptr<PipelineState> state)
    : RenderNode(std::move(config), std::move(state),
                 std::make_unique<render::OpenGLVideoRenderer>()) {}

RenderNode::RenderNode(render::RenderConfig config,
                       std::shared_ptr<PipelineState> state,
                       std::unique_ptr<render::IVideoRenderer> renderer)
    : config_(std::move(config)),
      state_(std::move(state)),
      renderer_(std::move(renderer)) {}

bool RenderNode::Init() {
    if (!renderer_) {
        renderer_ = std::make_unique<render::OpenGLVideoRenderer>();
    }
    if (!renderer_->Init(config_)) {
        std::cerr << "RenderNode: init failed\n";
        return false;
    }
    return true;
}

bool RenderNode::Start() {
    running_ = true;
    return true;
}

void RenderNode::Stop() {
    running_ = false;
}

void RenderNode::Deinit() {
    if (renderer_) {
        renderer_->Shutdown();
    }
}

std::string RenderNode::Name() const {
    return "rendernode";
}

void RenderNode::Process(std::shared_ptr<MediaFrame> frame) {
    if (!running_ || !frame || !renderer_) {
        return;
    }
    if (renderer_->ShouldClose()) {
        return;
    }

    if (!renderer_->Render(*frame)) {
        std::cerr << "RenderNode: render frame failed\n";
        return;
    }

    renderer_->PollEvents();
}

} // namespace pipeline
