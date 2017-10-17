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
    BaseSocket(int id)
        : _id(id)
    {
        CreateSocket();
        extern IOCP* g_ios;
        g_ios->Attach(this);
    }

    BaseSocket(int id, SOCKET fd)
        : _fd(fd)
        , _id(id)
    {
        extern IOCP* g_ios;
        g_ios->Attach(this);
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
    SOCKET _fd;
    int _id;
};

}
}
