#pragma once

#include "port/input_port.h"

namespace runtime {

template<typename In>
class SinkNode {
public:
    SinkNode() {
        input_.SetHandler([this](In data) {
            Process(std::move(data));
        });
    }

    InputPort<In>& Input() {
        return input_;
    }

protected:
    virtual void Process(In data) = 0;

private:
    InputPort<In> input_;
};

}
