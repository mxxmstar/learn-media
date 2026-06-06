#pragma once

namespace runtime {

class IRuntime {
public:
    virtual ~IRuntime() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
};

}
