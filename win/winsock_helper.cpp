#include <cassert>

#include "winsock_helper.h"

namespace taosocks {
namespace winsock {

LPFN_ACCEPTEX WSA::AcceptEx;
LPFN_GETACCEPTEXSOCKADDRS WSA::GetAcceptExSockAddrs;
LPFN_CONNECTEX WSA::ConnectEx;

void WSA::Startup()
{
    WSAData data;
    ::WSAStartup(MAKEWORD(2, 2), &data);

    _Init();
}

void WSA::Shutdown()
{
    ::WSACleanup();
}

void WSA::_Init()
{
    auto fd = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

    auto laGetFn = [fd](const GUID& guid) {
        void* fn;
        DWORD dwBytes;
        
        WSAIntRet ret = ::WSAIoctl(
            fd,
            SIO_GET_EXTENSION_FUNCTION_POINTER,
            const_cast<GUID*>(&guid), sizeof(guid),
            &fn, sizeof(void*),
            &dwBytes,
            nullptr,
            nullptr
        );

        return ret && fn != nullptr ? fn : nullptr;
    };

    const GUID guidAcceptEx = WSAID_ACCEPTEX;
    const GUID guidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
    const GUID guidConnectEx = WSAID_CONNECTEX;

    AcceptEx = (LPFN_ACCEPTEX)laGetFn(guidAcceptEx);
    GetAcceptExSockAddrs = (LPFN_GETACCEPTEXSOCKADDRS)laGetFn(guidGetAcceptExSockAddrs);
    ConnectEx = (LPFN_CONNECTEX)laGetFn(guidConnectEx);

    assert(AcceptEx && GetAcceptExSockAddrs && ConnectEx);

    ::closesocket(fd);
}

std::string to_string(const SOCKADDR_IN & r)
{
    // 111.111.111.111:12345
    char buf[22];
    sprintf(buf, "%d.%d.%d.%d:%d",
        r.sin_addr.S_un.S_un_b.s_b1,
        r.sin_addr.S_un.S_un_b.s_b2,
        r.sin_addr.S_un.S_un_b.s_b3,
        r.sin_addr.S_un.S_un_b.s_b4,
        ntohs(r.sin_port)
    );
    return buf;
}

}
}
