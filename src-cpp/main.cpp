#include "common/log/logmanager.h"
#include "decoder/decode_node.h"
#include "encoder/encode_node.h"
#include "infernode/async_inference_node.h"
#include "osd_node.h"
#include "pipeline/pipeline_types.h"
#include "puller/pull_stream_node.h"
#include "pusher/push_stream_node.h"
#include "runtime/runtime.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void HandleSignal(int) {
    g_stop_requested = 1;
}

std::string DefaultModelPath() {
    return (std::filesystem::path(__FILE__).parent_path() /
            "modules" / "inference" / "test" / "yolov5_model" / "yolov5s.xml")
        .string();
}

pipeline::PipelineOptions MakePipelineOptions() {
    pipeline::PipelineOptions options;

    // Fill these two addresses before running.
    options.input_url = "rtsp://192.168.66.218/live/mainstream";
    options.output_url = "rtsp://127.0.0.1:554/live/proxy_cam1";

    // Fill this when using another OpenVINO IR/ONNX model.
    options.model_path = DefaultModelPath();

    return options;
}

bool BuildGraph(runtime::Runtime& rt,
                const pipeline::PipelineOptions& options,
                const std::shared_ptr<pipeline::PipelineState>& state) {
    auto pull_exec = rt.CreateExecutor("media_pull", "media_pull_io", 1);
    auto decode_exec = rt.CreateExecutor("media_decode", "media_decode_cpu", 1);
    auto infer_exec = rt.CreateExecutor("media_infer", "media_infer_cpu", 1);
    auto osd_exec = rt.CreateExecutor("media_osd", "media_osd_cpu", 1);
    auto encode_exec = rt.CreateExecutor("media_encode", "media_encode_cpu", 1);
    auto push_exec = rt.CreateExecutor("media_push", "media_push_io", 1);

    auto& graph = rt.GetGraph();
    if (graph.AddNode<pipeline::PullStreamNode>("pull", pull_exec, options, state).empty() ||
        graph.AddNode<pipeline::DecodeNode>("decode", decode_exec, state).empty() ||
        graph.AddNode<pipeline::AsyncInferenceNode>("infer", infer_exec, options, state).empty() ||
        graph.AddNode<pipeline::OSDNode>("osd", osd_exec, state).empty() ||
        graph.AddNode<pipeline::EncodeNode>("encode", encode_exec, options, state).empty() ||
        graph.AddNode<pipeline::PushStreamNode>("push", push_exec, options, state).empty()) {
        LOG_MAIN_ERROR_AT("add node failed");
        return false;
    }

    const bool connected =
        graph.Connect<pipeline::PacketMessage>("pull", "decode",
                                               runtime::TransportType::Queue, 128,
                                               runtime::BackpressurePolicy::DropOldest) &&
        graph.Connect<pipeline::FrameMessage>("decode", "infer",
                                              runtime::TransportType::Queue, 16,
                                              runtime::BackpressurePolicy::DropOldest) &&
        graph.Connect<pipeline::InferenceMessagePtr>("infer", "osd",
                                                     runtime::TransportType::Queue, 16,
                                                     runtime::BackpressurePolicy::DropOldest) &&
        graph.Connect<pipeline::FrameMessage>("osd", "encode",
                                              runtime::TransportType::Queue, 16,
                                              runtime::BackpressurePolicy::DropOldest) &&
        graph.Connect<pipeline::PacketMessage>("encode", "push",
                                               runtime::TransportType::Queue, 64,
                                               runtime::BackpressurePolicy::DropOldest);
    if (!connected) {
        LOG_MAIN_ERROR_AT("connect graph failed");
        return false;
    }

    return true;
}

void LogStats(const pipeline::PipelineState& state) {
    LOG_MAIN_INFO_AT(
        "stats: pulled={}, decoded={}, infer_submitted={}, inferred={}, objects={}, "
        "osd={}, encoded={}, pushed={}, decode_err={}, infer_err={}, osd_err={}, "
        "encode_err={}, push_err={}",
        state.stats.pulled_packets.load(),
        state.stats.decoded_frames.load(),
        state.stats.inference_submitted.load(),
        state.stats.inferred_frames.load(),
        state.stats.detected_objects.load(),
        state.stats.osd_frames.load(),
        state.stats.encoded_packets.load(),
        state.stats.pushed_packets.load(),
        state.stats.decode_errors.load(),
        state.stats.inference_errors.load(),
        state.stats.osd_errors.load(),
        state.stats.encode_errors.load(),
        state.stats.push_errors.load());
}

} // namespace

int main() {
    LogManager::getInstance().Init();
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    const auto options = MakePipelineOptions();
    auto state = std::make_shared<pipeline::PipelineState>();

    runtime::Runtime rt;
    if (!BuildGraph(rt, options, state)) {
        LogManager::getInstance().FlushAll();
        return 1;
    }

    LOG_MAIN_INFO_AT("runtime async infer pipeline starting");
    LOG_MAIN_INFO_AT("input: {}", options.input_url);
    LOG_MAIN_INFO_AT("output: {}", options.output_url);
    LOG_MAIN_INFO_AT("model: {}", options.model_path);

    if (!rt.Start()) {
        LOG_MAIN_ERROR_AT("runtime start failed");
        rt.ShutdownAllPools();
        LogManager::getInstance().FlushAll();
        return 2;
    }

    int tick = 0;
    while (!g_stop_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++tick;
        if (tick % 5 == 0) {
            LogStats(*state);
        }
        if (state->stats.source_finished.load()) {
            LOG_MAIN_WARN_AT("source finished, stopping runtime");
            break;
        }
    }

    rt.Stop();
    rt.ShutdownAllPools();
    LogStats(*state);
    LOG_MAIN_INFO_AT("runtime async infer pipeline stopped");
    LogManager::getInstance().FlushAll();
    return 0;
}
