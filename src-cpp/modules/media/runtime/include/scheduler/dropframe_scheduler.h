#pragma once

#include "i_scheduler.h"
#include "graph/edge_context.h"

namespace runtime {

class DropFrameScheduler : public IScheduler {
public:
    void Notify(IEdgeContext* edge) override {
        auto* dst_ctx = edge->GetDestination();
        if (!dst_ctx || !dst_ctx->executor) return;
        dst_ctx->executor->Post([edge]() {
            edge->ExecuteDrain();
        });
    }
};

}
