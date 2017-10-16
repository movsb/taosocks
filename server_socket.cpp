#include "server_socket.h"
#include "log.h"

namespace taosocks {

void ServerSocket::Start(ULONG ip, USHORT port)
{
    SOCKADDR_IN addrServer = {0};
    addrServer.sin_family = AF_INET;
    addrServer.sin_addr.s_addr = ip;
    addrServer.sin_port = htons(port);

    if(::bind(_fd, (sockaddr*)&addrServer, sizeof(addrServer)) == SOCKET_ERROR)
        assert(0);

    if(::listen(_fd, SOMAXCONN) == SOCKET_ERROR)
        assert(0);

    _Accept();
}

int ServerSocket::GenId()
{
    return _next_id++;
}

void ServerSocket::OnAccept(std::function<void(ClientSocket*)> onAccepted)
{
    _onAccepted = onAccepted;
}

void ServerSocket::_OnAccepted(AcceptIOContext* io)
{
    SOCKADDR_IN *local, *remote;
    io->GetAddresses(&local, &remote);

    auto client = new ClientSocket(GenId(), _ios, _disp, io->fd, *local, *remote);
    _onAccepted(client);

    _Accept();
}

void ServerSocket::_Accept()
{
    for(;;) {
        auto acceptio = new AcceptIOContext();
        auto ret = acceptio->Accept(_fd);
        if(ret.Succ()) {
            // auto client = _OnAccepted(*acceptio);
            // clients.push_back(client);
            // LogLog("_Accept 立即完成：client fd:%d", client->GetDescriptor());
        }
        else if(ret.Fail()) {
            LogFat("_Accept 错误：code=%d", ret.Code());
            assert(0);
        }
        else if(ret.Async()) {
            // LogLog("_Accept 异步");
            break;
        }
    }
}

void ServerSocket::OnTask(BaseIOContext* bio)
{
    if(bio->optype == OpType::Accept) {
        _OnAccepted(static_cast<AcceptIOContext*>(bio));
    }
}

}

