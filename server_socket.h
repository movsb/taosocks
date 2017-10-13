#pragma once

#include <functional>
#include <vector>

#include "base_socket.h"
#include "winsock_helper.h"
#include "client_socket.h"

namespace taosocks {

namespace {

using namespace base_socket;
using namespace winsock;

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

    void GetAddresses(SOCKADDR_IN** local, SOCKADDR_IN** remote)
    {
        int len;

        WSA::GetAcceptExSockAddrs(
            buf,
            0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
            (sockaddr**)local, &len,
            (sockaddr**)remote, &len
        );
    }

    SOCKET fd;
    char buf[(sizeof(SOCKADDR_IN) + 16) * 2];
};

}

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
        : BaseSocket(-1, disp)
        , _next_id(0)
    {
    }
    
    void Start(ULONG ip, USHORT port);

    int GenId();

    void OnAccept(std::function<void(ClientSocket*)> onAccepted);

    ClientSocket* _OnAccepted(AcceptIOContext& io);

    void _Accept();

    virtual void OnDispatch(BaseDispatchData* data) override;

    virtual void OnTask(BaseIOContext& bio) override;

protected:
    int _next_id;
};

}