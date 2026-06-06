#pragma once

namespace runtime {

template<typename T>
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual bool Send(T data) = 0;
    virtual bool Receive(T& data) = 0;
};

}
