#pragma once
#include "tensordata/tensor_frame.h"
#include "defines/media_frame.hpp"
class IPreprocessor {
public:
    /// @brief 将MediaFrame转换为TensorFrame
    virtual TensorFrame Process(const MediaFrame&) = 0;
};