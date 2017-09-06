#include <cassert>
#include <process.h>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <mswsock.h>
#include <tchar.h>
#include <windows.h>

#include <iostream>
#include <vector>
#include <string>

#pragma comment(lib, "ws2_32")

static LPFN_ACCEPTEX s_fnAcceptEx;
static LPFN_GETACCEPTEXSOCKADDRS s_fnGetAcceptExSockAddrs;

static const size_t BUF_SIZE = 64 * 1024 * 1024;
static const TCHAR* const s_className = _T("{2FF09706-39AD-4FA0-B137-C4416E39C973}");

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

class WSARet
{
public:
    WSARet(bool b)
        : _b(b)
    {
        _e = ::WSAGetLastError();
    }

    bool Succ()     { return _b; }
    bool Fail()     { return !_b && _e != WSA_IO_PENDING; }
    bool Async()    { return !_b && _e == WSA_IO_PENDING; }
    int Code()      { return _e; }

private:
    bool _b;
    int _e;
};

class WSAIntRet : public WSARet
{
public:
    WSAIntRet(int value)
        : WSARet(value == 0)
    { }
};

class WSABoolRet : public WSARet
{
public:
    WSABoolRet(BOOL value)
        : WSARet(value != FALSE)
    { }
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
};

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

        WSABoolRet R = s_fnAcceptEx(
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

        s_fnGetAcceptExSockAddrs(
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

struct ReadIOContext;

struct ClientSocketContext
{
    ClientSocketContext(SOCKET fd)
        : fd(fd)
    {

    }

    SOCKET fd;
    SOCKADDR_IN local;
    SOCKADDR_IN remote;

    void _Read();
    void _Read(ReadIOContext* io);
};

struct ServerSocketContext
{
    HANDLE hiocp;
    SOCKET fd;
    HWND hwnd;

    ClientSocketContext* _OnAccept(AcceptIOContext* io)
    {
        auto client = new ClientSocketContext(io->fd);
        io->GetAddresses(&client->local, &client->remote);
        ::CreateIoCompletionPort((HANDLE)client->fd, hiocp, (ULONG_PTR)this, 0);
        return client;
    }
};


struct ReadIOContext : BaseIOContext
{
    ReadIOContext(ClientSocketContext* client)
        : BaseIOContext(OpType::Read)
        , client(client)
    {
        wsabuf.buf = buf;
        wsabuf.len = _countof(buf);
    }

    WSARet Read()
    {
        DWORD dwFlags = 0;

        WSAIntRet R = ::WSARecv(
            client->fd,
            &wsabuf, 1, nullptr,
            &dwFlags,
            &overlapped,
            nullptr
        );

        return R;
    }

    ClientSocketContext* client;
    WSABUF wsabuf;
    char buf[BUF_SIZE];
};

void ClientSocketContext::_Read()
{
    for(;;) {
        auto readio = new ReadIOContext(this);
        WSARet ret = readio->Read();
        if(ret.Succ()) {
            _Read(readio);
        }
        else if(ret.Fail()) {

        }
        else if(ret.Async()) {
            break;
        }
    }
}

void ClientSocketContext::_Read(ReadIOContext* io)
{
    DWORD dwBytes = 0;
    DWORD dwFlags = 0;
    WSAGetOverlappedResult(fd, &io->overlapped, &dwBytes, FALSE, &dwFlags);
    std::cout << std::string(io->wsabuf.buf, dwBytes);
}

static unsigned int __stdcall worker_thread(void* tag)
{
    HANDLE hIocp = static_cast<HANDLE>(tag);
    ServerSocketContext* pServerContext;
    BaseIOContext* pIoContext;
    DWORD dwBytesTransfered;

    while(true) {
        BOOL bRet = GetQueuedCompletionStatus(hIocp, &dwBytesTransfered, (PULONG_PTR)&pServerContext, (LPOVERLAPPED*)&pIoContext, INFINITE);
        assert(bRet != FALSE);

        if(pIoContext->optype == OpType::Accept) {
            auto acceptio = static_cast<AcceptIOContext*>(pIoContext);
            auto client = pServerContext->_OnAccept(acceptio);
            client->_Read();
        }
        else if(pIoContext->optype == OpType::Read) {
            auto readio = static_cast<ReadIOContext*>(pIoContext);
            readio->client->_Read(readio);
            readio->client->_Read();
        }
        else if(pIoContext->optype == OpType::Init) {
            auto initio = static_cast<InitIOContext*>(pIoContext);
            auto acceptio = new AcceptIOContext();
            auto ret = acceptio->Accept(pServerContext->fd);
            if(ret.Succ()) {
                auto client = pServerContext->_OnAccept(acceptio);
                client->_Read();
            }
            else if(ret.Async()) {

            }
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int main()
{
    HANDLE hIocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    assert(hIocp != nullptr);

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sckServer = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(sckServer != INVALID_SOCKET);

    auto ctx = new ServerSocketContext();
    ctx->hiocp = hIocp;
    ctx->fd = sckServer;
    hIocp = ::CreateIoCompletionPort((HANDLE)sckServer, hIocp, (ULONG_PTR)ctx, 0);
    assert(hIocp != nullptr);

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


    for(auto i = 0; i < 1; i++) {
        _beginthreadex(nullptr, 0, worker_thread, (void*)hIocp, 0, nullptr);
    }

    WNDCLASSEX wc = {sizeof(wc)};
    wc.hInstance = ::GetModuleHandle(nullptr);
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = s_className;
    ::RegisterClassEx(&wc);

    ctx->hwnd = ::CreateWindowEx(
        0, s_className, nullptr, 0, 
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, 
        nullptr, nullptr
    );

    assert(ctx->hwnd != nullptr);

    auto io = new InitIOContext();
    PostQueuedCompletionStatus(hIocp, 0, (ULONG_PTR)ctx, &io->overlapped);

    MSG msg;
    while(::GetMessage(&msg, nullptr, 0, 0)) {
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
