#pragma once
#include "tensordata/tensor_frame.h"
#include "defines/media_frame.hpp"
#include "inferenceinfo/result.h"
class IPostprocessor {
public:
    virtual FrameResult Process(const TensorFrame& output) = 0;
};