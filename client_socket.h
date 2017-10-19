#pragma once

#include <string>
#include <list>
#include <functional>

#include "base_socket.h"

namespace taosocks {

namespace {

using namespace base_socket;

struct ReadIOContext : BaseIOContext
{
    ReadIOContext()
        : BaseIOContext(OpType::Read)
    {
        wsabuf.buf = (char*)buf;
        wsabuf.len = _countof(buf);
    }

    WSARet Read(SOCKET fd)
    {
        DWORD dwFlags = 0;

        WSAIntRet R = ::WSARecv(
            fd,
            &wsabuf, 1,
            nullptr,
            &dwFlags,
            &overlapped,
            nullptr
        );

        return R;
    }

    WSABUF wsabuf;
    unsigned char buf[65536];
};

struct WriteIOContext : BaseIOContext
{
    WriteIOContext()
        : BaseIOContext(OpType::Write)
    {
        wsabuf.buf = nullptr;
    }
    ~WriteIOContext()
    {
        if(wsabuf.buf != nullptr) {
            delete[] wsabuf.buf;
            wsabuf.buf = nullptr;
        }
    }

    WSARet Write(SOCKET fd, const unsigned char* data, size_t size)
    {
        DWORD dwFlags = 0;

        wsabuf.buf = new char[size];
        ::memcpy(wsabuf.buf, data, size);
        //wsabuf.buf = (char*)data;
        wsabuf.len = size;

        WSAIntRet R = ::WSASend(
            fd,
            &wsabuf, 1,
            nullptr,
            dwFlags,
            &overlapped,
            nullptr
        );

        return R;
    }

    WSABUF wsabuf;
};

struct ConnectIOContext : BaseIOContext
{
    ConnectIOContext()
        : BaseIOContext(OpType::Connect)
    { }

    WSARet Connect(SOCKET fd, const sockaddr_in& addr)
    {
        WSABoolRet R = WSA::ConnectEx(
            fd, 
            (sockaddr*)&addr, sizeof(addr), 
            nullptr, 0,
            nullptr,
            &overlapped
        );

        return R;
    }
};


}

struct _CloseReason {
    enum Value {
        Actively,
        Passively,
        Reset,
    };
};

typedef _CloseReason::Value CloseReason;

class ClientSocket: public BaseSocket
{
private:
    struct Flags
    {
        enum Value
        {
            Closed          = 1 << 0,
            PendingRead     = 1 << 1,
        };
    };

public:
    ClientSocket(int id)
        : BaseSocket(id)
        , _flags(Flags::Closed)
    {

    }

    ClientSocket(int id, SOCKET fd, const SOCKADDR_IN& local, const SOCKADDR_IN& remote)
        : BaseSocket(id, fd)
        , _flags(0)
        , _local(local)
        , _remote(remote)
    {

    }

    typedef std::function<void(ClientSocket*, unsigned char*, size_t)> OnReadT;
    typedef std::function<void(ClientSocket*, size_t)> OnWriteT;
    typedef std::function<void(ClientSocket*, CloseReason reason)> OnCloseT;
    typedef std::function<void(ClientSocket*, bool)> OnConnectT;

private:
    SOCKADDR_IN _local;
    SOCKADDR_IN _remote;
    OnReadT _onRead;
    OnWriteT _onWrite;
    OnCloseT _onClose;
    OnConnectT _onConnect;
    DWORD _flags;
    std::list<ReadIOContext*> _read_queue;

public:
    void OnRead(OnReadT onRead);
    void OnWrite(OnWriteT onWrite);
    void OnClose(OnCloseT onClose);
    void OnConnect(OnConnectT onConnect);

    WSARet Connect(in_addr & addr, unsigned short port);
    WSARet Read();
    WSARet Write(const void* data, size_t size);
    void Close();
    bool IsClosed() const { return _flags & Flags::Closed; }

private:
    void _OnRead(ReadIOContext* io);
    void _OnWrite(WriteIOContext* io);
    void _OnClose(CloseReason reason);
    void _OnConnect(ConnectIOContext* io);


public:
    virtual void OnTask(BaseIOContext* bio) override;
};


}