#pragma once

#include "port/output_port.h"

namespace runtime {

template<typename Out>
class SourceNode {
public:
    OutputPort<Out>& Output() {
        return output_;
    }

protected:
    void Emit(Out data) {
        output_.Send(std::move(data));
    }

private:
    OutputPort<Out> output_;
};

}
