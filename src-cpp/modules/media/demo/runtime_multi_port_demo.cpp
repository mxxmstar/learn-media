#include "node/i_node.h"
#include "port/input_port.h"
#include "port/output_port.h"
#include "runtime/runtime.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace {

struct DemoState {
    std::mutex mutex;
    std::condition_variable cv;
    std::string joined;
    int length{0};
    bool accepted{false};
    bool has_joined{false};
    bool has_length{false};
    bool has_accepted{false};

    bool Complete() const {
        return has_joined && has_length && has_accepted;
    }
};

class SplitSourceNode : public runtime::INode {
public:
    bool RegisterPorts(runtime::PortRegistry& registry) {
        return registry.Register({
            runtime::PortSpec::Output<int>("number", number_out_),
            runtime::PortSpec::Output<std::string>("text", text_out_),
            runtime::PortSpec::Output<bool>("flag", flag_out_),
        });
    }

    bool Init() override { return true; }

    bool Start() override {
        number_out_.Send(7);
        text_out_.Send(std::string("seven"));
        flag_out_.Send(true);
        return true;
    }

    void Stop() override {}
    void Deinit() override {}
    std::string Name() const override { return "split_source"; }

private:
    runtime::OutputPort<int> number_out_;
    runtime::OutputPort<std::string> text_out_;
    runtime::OutputPort<bool> flag_out_;
};

class JoinNode : public runtime::INode {
public:
    JoinNode() {
        number_in_.SetHandler([this](int value) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                number_ = value;
            }
            TryEmit();
        });
        text_in_.SetHandler([this](std::string value) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                text_ = std::move(value);
            }
            TryEmit();
        });
        flag_in_.SetHandler([this](bool value) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                flag_ = value;
            }
            TryEmit();
        });
    }

    bool RegisterPorts(runtime::PortRegistry& registry) {
        return registry.Register({
            runtime::PortSpec::Input<int>("number", number_in_),
            runtime::PortSpec::Input<std::string>("text", text_in_),
            runtime::PortSpec::Input<bool>("flag", flag_in_),
            runtime::PortSpec::Output<std::string>("joined", joined_out_),
            runtime::PortSpec::Output<int>("length", length_out_),
            runtime::PortSpec::Output<bool>("accepted", accepted_out_),
        });
    }

    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}
    std::string Name() const override { return "join"; }

private:
    void TryEmit() {
        std::string joined;
        int length = 0;
        bool accepted = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!number_.has_value() || !text_.has_value() || !flag_.has_value() || emitted_) {
                return;
            }

            emitted_ = true;
            joined = std::to_string(*number_) + ":" + *text_ + ":" + (*flag_ ? "true" : "false");
            length = static_cast<int>(joined.size());
            accepted = *flag_;
        }

        joined_out_.Send(std::move(joined));
        length_out_.Send(length);
        accepted_out_.Send(accepted);
    }

    runtime::InputPort<int> number_in_;
    runtime::InputPort<std::string> text_in_;
    runtime::InputPort<bool> flag_in_;
    runtime::OutputPort<std::string> joined_out_;
    runtime::OutputPort<int> length_out_;
    runtime::OutputPort<bool> accepted_out_;
    std::mutex mutex_;
    std::optional<int> number_;
    std::optional<std::string> text_;
    std::optional<bool> flag_;
    bool emitted_{false};
};

class CollectSinkNode : public runtime::INode {
public:
    explicit CollectSinkNode(DemoState& state) : state_(state) {
        joined_in_.SetHandler([this](std::string value) {
            bool complete = false;
            {
                std::lock_guard<std::mutex> lock(state_.mutex);
                state_.joined = std::move(value);
                state_.has_joined = true;
                complete = state_.Complete();
            }
            if (complete) {
                state_.cv.notify_one();
            }
        });

        length_in_.SetHandler([this](int value) {
            bool complete = false;
            {
                std::lock_guard<std::mutex> lock(state_.mutex);
                state_.length = value;
                state_.has_length = true;
                complete = state_.Complete();
            }
            if (complete) {
                state_.cv.notify_one();
            }
        });

        accepted_in_.SetHandler([this](bool value) {
            bool complete = false;
            {
                std::lock_guard<std::mutex> lock(state_.mutex);
                state_.accepted = value;
                state_.has_accepted = true;
                complete = state_.Complete();
            }
            if (complete) {
                state_.cv.notify_one();
            }
        });
    }

    bool RegisterPorts(runtime::PortRegistry& registry) {
        return registry.Register({
            runtime::PortSpec::Input<std::string>("joined", joined_in_),
            runtime::PortSpec::Input<int>("length", length_in_),
            runtime::PortSpec::Input<bool>("accepted", accepted_in_),
        });
    }

    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}
    std::string Name() const override { return "collect_sink"; }

private:
    DemoState& state_;
    runtime::InputPort<std::string> joined_in_;
    runtime::InputPort<int> length_in_;
    runtime::InputPort<bool> accepted_in_;
};

} // namespace

int main() {
    DemoState state;
    runtime::Runtime rt;

    auto exec = rt.CreateExecutor("multi_port_demo", "multi_port_demo", 1);
    auto& graph = rt.GetGraph();

    if (graph.AddNode<SplitSourceNode>("source", exec).empty() ||
        graph.AddNode<JoinNode>("join", exec).empty() ||
        graph.AddNode<CollectSinkNode>("sink", exec, state).empty()) {
        return 1;
    }

    const bool connected =
        graph.Connect<int>({
            runtime::PortConnection{"source", "number", "join", "number"},
            runtime::PortConnection{"join", "length", "sink", "length"},
        }) &&
        graph.Connect<std::string>({
            runtime::PortConnection{"source", "text", "join", "text"},
            runtime::PortConnection{"join", "joined", "sink", "joined"},
        }) &&
        graph.Connect<bool>({
            runtime::PortConnection{"source", "flag", "join", "flag"},
            runtime::PortConnection{"join", "accepted", "sink", "accepted"},
        });

    if (!connected) {
        return 2;
    }

    if (!rt.Start()) {
        rt.ShutdownAllPools();
        return 3;
    }

    bool ok = false;
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        ok = state.cv.wait_for(lock, std::chrono::seconds(2), [&state]() {
            return state.Complete();
        });
        ok = ok &&
             state.joined == "7:seven:true" &&
             state.length == 12 &&
             state.accepted;
    }

    rt.Stop();
    rt.ShutdownAllPools();
    return ok ? 0 : 4;
}
