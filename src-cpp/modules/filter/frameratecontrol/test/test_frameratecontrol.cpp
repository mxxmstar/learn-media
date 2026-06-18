#include "adaptive_policy.h"
#include "compose_policy.h"
#include "controller_adapter.h"
#include "fixed_policy.h"
#include "pace_controller.h"
#include "queue_controller.h"
#include "queue_policy.h"
#include "sample_controller.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>

namespace {

Timestamp tick(int index, int ms_step = 33) {
    static const Timestamp base = std::chrono::steady_clock::now();
    return base + std::chrono::milliseconds(index * ms_step);
}

void test_policies() {
    FixedPolicy fixed(15.0);
    assert(fixed.TargetFps() == 15.0);

    AdaptivePolicy adaptive({
        30.0,
        5.0,
        60.0,
        Duration{100},
        0.8,
        0.8,
        1.05
    });
    adaptive.Update(FrameRateFeedback{Duration{250}, 0.1, 0.0, 0.0, 0.0});
    assert(adaptive.TargetFps() < 30.0);
    assert(adaptive.TargetFps() >= 5.0);

    QueuePolicy queue(30.0);
    queue.Update(FrameRateFeedback{Duration{}, 1.0, 0.0, 0.0, 0.0});
    assert(queue.TargetFps() < 30.0);

    auto fixed_policy = std::make_shared<FixedPolicy>(25.0);
    auto queue_policy = std::make_shared<QueuePolicy>(30.0);
    CompositePolicy composite;
    composite.Add(fixed_policy);
    composite.Add(queue_policy);
    composite.Update(FrameRateFeedback{Duration{}, 1.0, 0.0, 0.0, 0.0});
    assert(composite.TargetFps() == queue_policy->TargetFps());
}

void test_pace_controller() {
    FrameRateConfig cfg;
    cfg.target_fps = 10.0;

    PaceFrameRateController<MediaFrame> controller(cfg);
    MediaFrame frame;

    assert(controller.Accept(frame, tick(0, 10)));
    assert(!controller.Accept(frame, tick(1, 10)));
    assert(controller.Accept(frame, tick(10, 10)));

    auto stats = controller.Stats();
    assert(stats.passed == 2);
    assert(stats.dropped == 1);
    assert(stats.drop_ratio > 0.3 && stats.drop_ratio < 0.34);
}

void test_sample_controller() {
    FrameRateConfig cfg;
    cfg.input_fps = 30.0;
    cfg.target_fps = 10.0;

    SampleFrameRateController<MediaFrame> controller(cfg);
    MediaFrame frame;

    int accepted = 0;
    for (int i = 0; i < 30; ++i) {
        if (controller.Accept(frame, tick(i))) {
            ++accepted;
        }
    }

    assert(accepted == 10);
    assert(controller.Stats().dropped == 20);
}

void test_queue_controller() {
    FrameRateConfig cfg;
    cfg.max_queue = 2;

    QueueFrameRateController<MediaFrame> controller(cfg);
    MediaFrame frame;

    assert(controller.Accept(frame, tick(0)));
    assert(controller.Accept(frame, tick(1)));
    assert(!controller.Accept(frame, tick(2)));

    controller.OnFrameSent(tick(3));
    assert(controller.Accept(frame, tick(4)));
}

void test_adapter() {
    FrameRateConfig cfg;
    cfg.target_fps = 30.0;

    auto controller = std::make_shared<PaceFrameRateController<MediaFrame>>(cfg);
    auto policy = std::make_shared<FixedPolicy>(5.0);
    FrameRateAdapter<MediaFrame> adapter(controller, policy);

    MediaFrame frame;
    assert(adapter.Accept(frame, tick(0)));
    assert(adapter.GetTargetFps() == 5.0);

    adapter.Feedback(FrameRateFeedback{Duration{80}, 0.0, 0.0, 0.0, 0.0});
    assert(adapter.TargetFps() == 5.0);
}

}  // namespace

int main() {
    test_policies();
    test_pace_controller();
    test_sample_controller();
    test_queue_controller();
    test_adapter();

    std::cout << "frameratecontrol tests passed" << std::endl;
    return 0;
}
