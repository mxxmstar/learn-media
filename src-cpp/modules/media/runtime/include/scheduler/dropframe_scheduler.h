#pragma once

#include "i_scheduler.h"
#include "executor/i_executor.h"
#include "graph/edge_context.h"

namespace runtime {

class DropFrameScheduler : public IScheduler {
public:
    bool Notify(std::shared_ptr<IEdgeContext> edge) override {
        if (!edge) {
            return false;
        }

        auto* dst_ctx = edge->GetDestination();
        if (!dst_ctx || !dst_ctx->executor) {
            return false;
        }

        return dst_ctx->executor->Post([edge = std::move(edge)]() {
            edge->ExecuteDrain();
        });
    }
};

}
