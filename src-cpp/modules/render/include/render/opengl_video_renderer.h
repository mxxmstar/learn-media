#pragma once

#include <cstdint>
#include <string>

#include "render/frame_converter.h"
#include "render/i_video_renderer.h"

struct GLFWwindow;

namespace render {

class OpenGLVideoRenderer final : public IVideoRenderer {
public:
    OpenGLVideoRenderer() = default;
    ~OpenGLVideoRenderer() override;

    bool Init(const RenderConfig& config) override;
    bool Render(const MediaFrame& frame) override;
    void PollEvents() override;
    bool ShouldClose() const override;
    void Shutdown() override;

private:
    bool CreateWindow();
    bool CreateProgram();
    bool CreateQuad();
    bool UploadTexture(const RgbaFrame& frame);
    void DestroyGlResources();
    void SetError(std::string message);

    RenderConfig config_;
    FrameConverter converter_;
    RgbaFrame rgba_frame_;
    GLFWwindow* window_{nullptr};
    std::uint32_t program_{0};
    std::uint32_t vao_{0};
    std::uint32_t vbo_{0};
    std::uint32_t ebo_{0};
    std::uint32_t texture_{0};
    int texture_width_{0};
    int texture_height_{0};
    bool glfw_initialized_{false};
    bool initialized_{false};
    std::string last_error_;
};

} // namespace render
