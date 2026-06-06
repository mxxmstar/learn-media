#pragma once

#include "port/input_port.h"
#include "port/output_port.h"

namespace runtime {

template<typename In, typename Out>
class TransformNode {
public:
    TransformNode() {
        input_.SetHandler([this](In data) {
            Process(std::move(data));
        });
    }

    InputPort<In>& Input() {
        return input_;
    }

    OutputPort<Out>& Output() {
        return output_;
    }

protected:
    virtual void Process(In data) = 0;

    void Emit(Out data) {
        output_.Send(std::move(data));
    }

private:
    InputPort<In> input_;
    OutputPort<Out> output_;
};

}
