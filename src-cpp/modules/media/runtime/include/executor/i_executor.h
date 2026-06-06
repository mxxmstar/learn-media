#pragma once

#include <functional>

namespace runtime {

class IExecutor {
public:
    using Task = std::function<void()>;

    virtual ~IExecutor() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool Post(Task task) = 0;
};

}
