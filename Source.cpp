#include <cassert>
#include <process.h>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <mswsock.h>
#include <windows.h>

#include <vector>

#pragma comment(lib, "ws2_32")

static LPFN_ACCEPTEX s_fnAcceptEx;
static LPFN_GETACCEPTEXSOCKADDRS s_fnGetAcceptExSockAddrs;
static HANDLE s_hIocp;

static const size_t BUF_SIZE = 64 * 1024 * 1024;

struct OpType
{
    enum Value
    {
        Init,
        Accept,
        Read,
        Write,
    };
};

struct PerIOContext
{
    OVERLAPPED _overlapped;
    SOCKET _sckClient;
    OpType::Value _op;
    WSABUF _wsabuf;
    char _buf[BUF_SIZE];

    PerIOContext()
    {
        memset(this, 0, sizeof(*this));
        _wsabuf.len = _countof(_buf);
        _wsabuf.buf = &_buf[0];
    }
};

struct PerSocketContext
{
    SOCKET _sckCliet;
    SOCKADDR_IN _addrClient;
    SOCKADDR_IN _addrRemote;
    std::vector<PerIOContext*> _ios;
};

static unsigned int __stdcall worker_thread(void* tag)
{
    PerSocketContext* pSocketContext;
    PerIOContext* pIoContext;
    DWORD dwBytesTransfered;
    while(true) {
        BOOL bRet = GetQueuedCompletionStatus(s_hIocp, &dwBytesTransfered, (PULONG_PTR)&pSocketContext, (LPOVERLAPPED*)&pIoContext, INFINITE);
        assert(bRet != FALSE);

        if(pIoContext->_op == OpType::Accept) {
            auto ctx = new PerSocketContext();
            int len;
            SOCKADDR_IN* pClientAddr;
            SOCKADDR_IN* pRemoteAddr;
            s_fnGetAcceptExSockAddrs(pIoContext->_buf,
                0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, 
                (sockaddr**)&pClientAddr, &len, (sockaddr**)&pRemoteAddr, &len);
            memcpy(&ctx->_addrClient, pClientAddr, sizeof(SOCKADDR_IN));
            memcpy(&ctx->_addrRemote, pRemoteAddr, sizeof(SOCKADDR_IN));

            auto io = new PerIOContext();
            io->_op = OpType::Read;
            int ret = WSARecv(pIoContext->_sckClient, &io->_wsabuf, 1, nullptr, &dwBytesTransfered, &io->_overlapped, nullptr);
            ret = ret;
        }
        else if(pIoContext->_op == OpType::Read) {

        }
        else if(pIoContext->_op == OpType::Write) {

        
        }
        else if(pIoContext->_op == OpType::Init) {
            auto io = new PerIOContext();
            io->_op = OpType::Accept;
            io->_sckClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            assert(io->_sckClient != INVALID_SOCKET);
            DWORD dwBytes;
            BOOL bRet = s_fnAcceptEx(
                pSocketContext->_sckCliet,
                io->_sckClient, &io->_buf,
                0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                &dwBytes,
                &io->_overlapped
            );
        }
    }
}

int main()
{
    HANDLE hIocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    assert(hIocp != nullptr);
    s_hIocp = hIocp;

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sckServer = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(sckServer != INVALID_SOCKET);

    auto ctx = new PerSocketContext();
    ctx->_sckCliet = sckServer;
    ::CreateIoCompletionPort((HANDLE)sckServer, hIocp, (ULONG_PTR)ctx, 0);

    SOCKADDR_IN addrServer = {0};
    addrServer.sin_family = AF_INET;
    addrServer.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrServer.sin_port = htons(8080);

    if(bind(sckServer, (sockaddr*)&addrServer, sizeof(addrServer)) == SOCKET_ERROR)
        assert(0);

    if(listen(sckServer, SOMAXCONN) == SOCKET_ERROR)
        assert(0);

    DWORD dwBytes = 0;
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    GUID guidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;

    WSAIoctl(
        sckServer,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidAcceptEx, sizeof(guidAcceptEx),
        &s_fnAcceptEx, sizeof(s_fnAcceptEx),
        &dwBytes,
        nullptr, nullptr
        );

    WSAIoctl(
        sckServer,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidGetAcceptExSockAddrs, sizeof(guidGetAcceptExSockAddrs),
        &s_fnGetAcceptExSockAddrs, sizeof(s_fnGetAcceptExSockAddrs),
        &dwBytes,
        nullptr, nullptr
        );


    assert(s_fnAcceptEx != nullptr);
    assert(s_fnGetAcceptExSockAddrs != nullptr);


    for(auto i = 0; i < 10; i++) {
        _beginthreadex(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    }

    auto io = new PerIOContext();
    io->_op = OpType::Init;
    PostQueuedCompletionStatus(hIocp, 0, (ULONG_PTR)ctx, &io->_overlapped);

    Sleep(INFINITE);
}
