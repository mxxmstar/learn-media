#pragma once

namespace runtime {

class IEdgeContext;

class IScheduler {
public:
    virtual ~IScheduler() = default;
    virtual void Notify(IEdgeContext* edge) = 0;
};

}
