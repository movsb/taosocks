#pragma once

#include <functional>
#include <vector>

#include "base_socket.h"
#include "winsock_helper.h"
#include "client_socket.h"

#define LogLog(...)
#define LogFat(...)

namespace taosocks {

namespace {

using namespace base_socket;
using namespace winsock;

struct InitIOContext : BaseIOContext
{
    InitIOContext()
        : BaseIOContext(OpType::Init)
    { }
};


struct AcceptIOContext : BaseIOContext
{
    AcceptIOContext()
        : BaseIOContext(OpType::Accept)
    {
        fd = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        assert(fd != INVALID_SOCKET);
    }

    WSARet Accept(SOCKET listen)
    {
        DWORD dwBytes;

        WSABoolRet R = WSA::AcceptEx(
            listen,
            fd,
            buf,
            0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
            &dwBytes,
            &overlapped
        );

        return R;
    }

    void GetAddresses(SOCKADDR_IN* local, SOCKADDR_IN* remote)
    {
        int len;
        SOCKADDR_IN* _local;
        SOCKADDR_IN* _remote;

        WSA::GetAcceptExSockAddrs(
            buf,
            0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
            (sockaddr**)&_local, &len,
            (sockaddr**)&_remote, &len
        );

        std::memcpy(local, _local, sizeof(SOCKADDR_IN));
        std::memcpy(remote, _remote, sizeof(SOCKADDR_IN));
    }

    SOCKET fd;
    char buf[(sizeof(SOCKADDR_IN) + 16) * 2];
};

}

struct ClientSocket;

struct ServerSocket: public BaseSocket
{
    std::function<void(ClientSocket*)> _onAccepted;

    struct AcceptDispatchData : BaseDispatchData
    {
        AcceptDispatchData()
            : BaseDispatchData(OpType::Accept)
        { }

        ClientSocket* client;
    };

public:
    ServerSocket(threading::Dispatcher& disp)
        : BaseSocket(disp)
    {
    }
    
    void Start(ULONG ip, USHORT port);

    void OnAccepted(std::function<void(ClientSocket*)> onAccepted);

    ClientSocket* _OnAccepted(AcceptIOContext& io);

    std::vector<ClientSocket*> _Accept();

    virtual void Invoke(BaseDispatchData& data) override;
    virtual void Handle(BaseIOContext& bio) override;
};

}