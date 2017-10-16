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
        Close,
        Accept,
        Read,
        Write,
        Connect,
        __End,
    };
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

class BaseSocket : private threading::IDispatcher, public iocp::ITaskHandler
{
public:
    BaseSocket(int id, IOCP& ios, Dispatcher& disp)
        : _id(id)
        , _ios(ios)
        , _disp(disp)
    {
        CreateSocket();
        ios.Attach(this);
    }

    BaseSocket(int id, IOCP& ios, Dispatcher& disp, SOCKET fd)
        : _disp(disp)
        , _ios(ios)
        , _fd(fd)
        , _id(id)
    {
        ios.Attach(this);
    }

    SOCKET GetSocket() { return _fd; }
    virtual HANDLE GetHandle() override;
    int GetId() { return _id; }

private:
    void CreateSocket();

protected:
    virtual void OnTask(BaseIOContext* bio) = 0;

private:
    virtual void OnDispatch(void* data) override;
    virtual void OnTask(OVERLAPPED* overlapped) override;

protected:
    IOCP& _ios;
    Dispatcher& _disp;
    SOCKET _fd;
    int _id;
};

}
}
