#pragma once
#include "postprocessor/i_postprocessor.h"
#include "inferenceinfo/result.h"
#include "tensordata/tensor_frame.h"

/// @brief OpenVINO YOLO 后处理器配置
struct OpenVinoYoloPostprocessorConfig {
    
};

class YoloPostprocessor : public IPostprocessor {
public:

    bool Initialize(const OpenVinoYoloPostprocessorConfig& config);


    FrameResult Process(const TensorFrame& output) override;


private:

    /// @brief 根据布局将 TensorPlane 解析为DetectionBox
    std::vector<DetectionBox> decode(const TensorPlane& tensor);


    std::vector<DetectionBox> nms(std::vector<DetectionBox>& detections);

    
    Rectangle restoreBox(const Rectangle& box);


private:

    float conf_threshold_{0.25f};

    float nms_threshold_{0.45f};


    int input_width_{640};

    int input_height_{640};


    int original_width_;

    int original_height_;


    LetterBoxInfo letterbox_;
};