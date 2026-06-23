#pragma once

#include <memory>
#include <string>

#include "defines/media_frame.hpp"
#include "node/i_node.h"
#include "node/sink_node.h"
#include "render/i_video_renderer.h"
#include "render/render_config.h"

namespace pipeline {

class PipelineState;

class RenderNode : public runtime::INode,
                   public runtime::SinkNode<std::shared_ptr<MediaFrame>> {
public:
    explicit RenderNode(std::shared_ptr<PipelineState> state);
    RenderNode(render::RenderConfig config, std::shared_ptr<PipelineState> state);
    RenderNode(render::RenderConfig config,
               std::shared_ptr<PipelineState> state,
               std::unique_ptr<render::IVideoRenderer> renderer);

    bool Init() override;
    bool Start() override;
    void Stop() override;
    void Deinit() override;
    std::string Name() const override;

protected:
    void Process(std::shared_ptr<MediaFrame> frame) override;

private:
    render::RenderConfig config_;
    std::shared_ptr<PipelineState> state_;
    std::unique_ptr<render::IVideoRenderer> renderer_;
    bool running_{false};
};

} // namespace pipeline
