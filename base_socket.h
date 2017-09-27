#pragma once

#include <cstring>

#include "winsock_helper.h"
#include "thread_dispatcher.h"
#include "iocp_model.h"

namespace taosocks {
namespace base_socket {

using namespace winsock;

struct OpType
{
    enum Value
    {
        __Beg,
        Init,
        Close,
        Accept,
        Read,
        Write,
        Connect,
        __End,
    };
};

struct BaseDispatchData
{
    BaseDispatchData(OpType::Value type)
        : optype(type)
    {
        assert(OpType::__Beg < type && type < OpType::__End);
    }

    OpType::Value optype;
};

struct BaseIOContext
{
    OVERLAPPED overlapped;
    OpType::Value optype;

    BaseIOContext(OpType::Value op)
        : optype(op)
    {
        std::memset(&overlapped, 0, sizeof(overlapped));
    }

    WSARet GetResult(SOCKET fd, DWORD* pdwBytes = nullptr, DWORD* pdwFlags = nullptr);
};

class BaseSocket : private threading::IDispatcher, public iocp::TaskHandler
{
public:
    BaseSocket(threading::Dispatcher& disp)
        : _disp(disp)
    {
        CreateSocket();
    }

    BaseSocket(threading::Dispatcher& disp, SOCKET fd)
        : _disp(disp)
        , _fd(fd)
    {

    }

    SOCKET GetSocket() { return _fd; }

private:
    void CreateSocket();

protected:
    void Dispatch(BaseDispatchData& data);
    
protected:
    virtual void Invoke(BaseDispatchData& data) = 0;
    virtual void Handle(BaseIOContext& bio) = 0;

private:
    virtual void Invoke(void* data) override;
    virtual void Handle(OVERLAPPED* overlapped) override;

protected:
    threading::Dispatcher& _disp;
    SOCKET _fd;
};

}
}
