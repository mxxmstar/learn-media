#pragma once

#include <memory>
#include <string>

#include "stream/stream_info.h"

class MediaPacket;

class IPuller {
public:
    virtual ~IPuller() = default;

    virtual bool Open(const std::string& url) = 0;
    virtual void Close() = 0;
    virtual StreamInfo GetStreamInfo() = 0;
    virtual bool ReadPacket(std::shared_ptr<MediaPacket>& packet) = 0;
};
