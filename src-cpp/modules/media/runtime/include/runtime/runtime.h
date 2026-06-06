#pragma once

#include <memory>
#include <vector>

#include "graph/graph.h"
#include "scheduler/fifo_scheduler.h"
#include "executor/asio_executor.h"

namespace runtime {

class Runtime {
public:
    Runtime() {
        graph_.SetScheduler(std::make_shared<FIFOScheduler>());
    }

    Graph& GetGraph() {
        return graph_;
    }

    std::shared_ptr<AsioExecutor> CreateExecutor(const std::string& name,
                                                  const std::string& pool_name = "general",
                                                  std::size_t pool_size = 0) {
        auto exec = std::make_shared<AsioExecutor>(name, pool_name, pool_size);
        executors_.push_back(exec);
        return exec;
    }

    bool Start() {
        return graph_.Start();
    }

    void Stop() {
        graph_.Stop();
    }

    void ShutdownAllPools() {
        AsioExecutor::StopAllPools();
    }

private:
    Graph graph_;
    std::vector<std::shared_ptr<IExecutor>> executors_;
};

}
