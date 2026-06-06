#pragma once

#include <string>

namespace runtime {

class INode {
public:
    virtual ~INode() = default;
    virtual bool Init() = 0;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void Deinit() = 0;
    virtual std::string Name() const = 0;
};

}
