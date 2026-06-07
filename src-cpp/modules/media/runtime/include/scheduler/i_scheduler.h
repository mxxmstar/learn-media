#pragma once

#include <memory>

namespace runtime {

class IEdgeContext;

class IScheduler {
public:
    virtual ~IScheduler() = default;
    virtual bool Notify(std::shared_ptr<IEdgeContext> edge) = 0;
};

}
