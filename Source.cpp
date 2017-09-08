#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <cassert>
#include <process.h>
#include <WinSock2.h>
#include <mswsock.h>
#include <tchar.h>
#include <windows.h>

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <functional>

#pragma comment(lib, "ws2_32")

static LPFN_ACCEPTEX s_fnAcceptEx;
static LPFN_GETACCEPTEXSOCKADDRS s_fnGetAcceptExSockAddrs;

static const size_t BUF_SIZE = 64 * 1024 * 1024;
static const TCHAR* const s_className = _T("{2FF09706-39AD-4FA0-B137-C4416E39C973}");

static std::string to_string(const SOCKADDR_IN& r)
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

struct ISocketDispatcher
{
    virtual void Invoke(void* data) = 0;
};

struct SocketDispatcher
{
    HWND hwnd;
    static const UINT msg = WM_USER + 0;

    SocketDispatcher()
    {
        Init();
    }

    void Dispatch(ISocketDispatcher* disp, void* data)
    {
        ::SendMessage(hwnd, msg, WPARAM(disp), LPARAM(data));
    }

    void Init()
    {
        WNDCLASSEX wc = {sizeof(wc)};
        wc.hInstance = ::GetModuleHandle(nullptr);
        wc.lpfnWndProc = __WndProc;
        wc.lpszClassName = s_className;
        wc.cbWndExtra = sizeof(void*);
        ::RegisterClassEx(&wc);

        hwnd = ::CreateWindowEx(
            0, s_className, nullptr, 0, 
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, 
            nullptr,
            this
        );

        assert(hwnd != nullptr);
    }

    int Run()
    {
        MSG msg;
        while(::GetMessage(&msg, nullptr, 0, 0)) {
            DispatchMessage(&msg);
        }
        return (int)msg.wParam;
    }

    LRESULT WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if(uMsg == msg) {
            auto pDisp = reinterpret_cast<ISocketDispatcher*>(wParam);
            auto pData = reinterpret_cast<void*>(lParam);
            pDisp->Invoke(pData);
            return 0;
        }

        return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    static LRESULT __stdcall __WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        auto pThis = (SocketDispatcher*)::GetWindowLongPtr(hWnd, 0);

        switch(uMsg)
        {
        case WM_NCCREATE:
            pThis = (SocketDispatcher*)LPCREATESTRUCT(lParam)->lpCreateParams;
            ::SetWindowLongPtr(hWnd, 0, (LONG)pThis);
            break;
        case WM_NCDESTROY:
            pThis = nullptr;
            ::SetWindowLongPtr(hWnd, 0, 0);
            break;
        }

        return pThis
            ? pThis->WndProc(hWnd, uMsg, wParam, lParam)
            : ::DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
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
    unsigned char buf[BUF_SIZE];
};

struct WriteIOContext : BaseIOContext
{
    WriteIOContext()
        : BaseIOContext(OpType::Write)
    { }

    WSARet Write(SOCKET fd, const unsigned char* data, size_t size)
    {
        DWORD dwFlags = 0;

        wsabuf.buf = (char*)data;
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

struct BaseSocketContext
{

};

struct ClientSocketContext : public BaseSocketContext, public ISocketDispatcher
{
    ClientSocketContext(SocketDispatcher* disp, SOCKET fd)
        : fd(fd)
        , _disp(disp)
    {

    }

    SOCKET fd;
    SOCKADDR_IN local;
    SOCKADDR_IN remote;
    typedef std::function<void(ClientSocketContext*, unsigned char*, size_t)> OnReadT;
    typedef std::function<void(ClientSocketContext*, size_t)> OnWrittenT;
    OnReadT _onRead;
    OnWrittenT _onWritten;
    SocketDispatcher* _disp;

    void Close()
    {
        ::closesocket(fd);
    }

    void OnRead(OnReadT onRead)
    {
        _onRead = onRead;
    }

    void OnWritten(OnWrittenT onWritten)
    {
        _onWritten = onWritten;
    }

    void Write(const char* data, size_t size, void* tag)
    {
        return Write((const unsigned char*)data, size, tag);
    }

    void Write(const char* data, void* tag)
    {
        return Write(data, std::strlen(data), tag);
    }

    void Write(const unsigned char* data, size_t size, void* tag)
    {
        auto writeio = new WriteIOContext();
        auto ret = writeio->Write(fd, data, size);
        if(ret.Succ()) {

        }
        else if(ret.Fail()) {
            assert(0);
        }
        else if(ret.Async()) {

        }
    }

    void _Read()
    {
        for(;;) {
            auto readio = new ReadIOContext();
            WSARet ret = readio->Read(fd);
            if(ret.Succ()) {

            }
            else if(ret.Fail()) {
                assert(0);
            }
            else if(ret.Async()) {
                break;
            }
        }
    }

    void _OnRead(ReadIOContext* io, bool inThread)
    {
        if(inThread) {
            return _disp->Dispatch(this, static_cast<BaseIOContext*>(io));
        }

        DWORD dwBytes = 0;
        DWORD dwFlags = 0;
        WSAGetOverlappedResult(fd, &io->overlapped, &dwBytes, FALSE, &dwFlags);
        _onRead(this, io->buf, dwBytes);
    }

    void _OnWritten(WriteIOContext* io, bool inThread)
    {
        if(inThread) {
            return _disp->Dispatch(this, static_cast<BaseIOContext*>(io));
        }

        DWORD dwBytes = 0;
        DWORD dwFlags = 0;
        WSAGetOverlappedResult(fd, &io->overlapped, &dwBytes, FALSE, &dwFlags);
        _onWritten(this, dwBytes);
    }

    virtual void Invoke(void * data) override
    {
        auto pBaseContext = static_cast<BaseIOContext*>(data);
        if(pBaseContext->optype == OpType::Read) {
            auto io = static_cast<ReadIOContext*>(pBaseContext);
            _OnRead(io, false);
        }
        else if(pBaseContext->optype == OpType::Write) {
            auto io = static_cast<WriteIOContext*>(pBaseContext);
            _OnWritten(io, false);
        }
    }
};

struct ServerSocketContext : public BaseSocketContext
{
    HANDLE hiocp;
    SOCKET fd;
    HWND hwnd;
    std::function<void(ClientSocketContext*)> _onAccepted;
    SocketDispatcher* _disp;

    ServerSocketContext(SocketDispatcher* disp)
    {
        _disp = disp;
    }

    void OnAccepted(std::function<void(ClientSocketContext*)> onAccepted)
    {
        _onAccepted = onAccepted;
    }

    ClientSocketContext* _OnAccepted(AcceptIOContext* io)
    {
        auto client = new ClientSocketContext(_disp, io->fd);
        io->GetAddresses(&client->local, &client->remote);
        ::CreateIoCompletionPort((HANDLE)client->fd, hiocp, (ULONG_PTR)static_cast<BaseSocketContext*>(client), 0);
        _onAccepted(client);
        return client;
    }

    std::vector<ClientSocketContext*> _Accept()
    {
        std::vector<ClientSocketContext*> clients;

        for(;;) {
            auto acceptio = new AcceptIOContext();
            auto ret = acceptio->Accept(fd);
            if(ret.Succ()) {
                auto client = _OnAccepted(acceptio);
                clients.push_back(client);
            }
            else if(ret.Fail()) {
                assert(0);
            }
            else if(ret.Async()) {
                break;
            }
        }

        return std::move(clients);
    }
};

static unsigned int __stdcall worker_thread(void* tag)
{
    HANDLE hIocp = static_cast<HANDLE>(tag);
    BaseSocketContext* pBaseSocket;
    ServerSocketContext* pServerContext;
    ClientSocketContext* pClientSocket;
    BaseIOContext* pIoContext;
    ReadIOContext* readio;
    WriteIOContext* writeio;
    AcceptIOContext* acceptio;
    DWORD dwBytesTransfered;

    while(true) {
        BOOL bRet = GetQueuedCompletionStatus(hIocp, &dwBytesTransfered, (PULONG_PTR)&pBaseSocket, (LPOVERLAPPED*)&pIoContext, INFINITE);
        assert(bRet != FALSE);

        if(pIoContext->optype == OpType::Accept) {
            pServerContext = static_cast<ServerSocketContext*>(pBaseSocket);
            acceptio = static_cast<AcceptIOContext*>(pIoContext);
            pClientSocket = pServerContext->_OnAccepted(acceptio);
            pClientSocket->_Read();
        }
        else if(pIoContext->optype == OpType::Read) {
            pClientSocket = static_cast<ClientSocketContext*>(pBaseSocket);
            readio = static_cast<ReadIOContext*>(pIoContext);
            pClientSocket->_OnRead(readio, true);
            pClientSocket->_Read();
        }
        else if(pIoContext->optype == OpType::Write) {
            pClientSocket = static_cast<ClientSocketContext*>(pBaseSocket);
            writeio = static_cast<WriteIOContext*>(pIoContext);
            pClientSocket->_OnWritten(writeio, true);
        }
        else if(pIoContext->optype == OpType::Init) {
            auto initio = static_cast<InitIOContext*>(pIoContext);
            delete initio;
            pServerContext = static_cast<ServerSocketContext*>(pBaseSocket);
            auto clients = pServerContext->_Accept();
            for(auto& client : clients) {
                client->_Read();
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

    auto dispatcher = new SocketDispatcher();

    auto ctx = new ServerSocketContext(dispatcher);
    ctx->hiocp = hIocp;
    ctx->fd = sckServer;
    hIocp = ::CreateIoCompletionPort((HANDLE)sckServer, hIocp, (ULONG_PTR)ctx, 0);
    assert(hIocp != nullptr);

    SOCKADDR_IN addrServer = {0};
    addrServer.sin_family = AF_INET;
    addrServer.sin_addr.s_addr = inet_addr("0.0.0.0");
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

    static std::string res =
        "HTTP/1.1 200 OK\r\n"
        "Server: iocp test\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<!doctype html>\n"
        "<html>\n"
        "<head>\n"
        "<meta charset=\"utf-8\" />\n"
        "</head>\n"
        "<body>\n"
        "<h1>Head</h1>\n"
        "<p>Paragraph</p>\n"
        "</body>\n"
        "</html>"
        ;

    ctx->OnAccepted([](ClientSocketContext* client) {
        std::printf("客户端连接：%s\n", to_string(client->remote).c_str());
        client->OnRead([](ClientSocketContext* client, unsigned char* data, size_t size) {
            std::printf("接收到数据：%.*s\n", size, data);
            client->Write(res.c_str(), nullptr);
        });
        client->OnWritten([](ClientSocketContext* client, size_t size) {
            std::printf("发送了数据：%d 字节\n", size);
            client->Close();
        });
    });

    auto io = new InitIOContext();
    PostQueuedCompletionStatus(hIocp, 0, (ULONG_PTR)ctx, &io->overlapped);

    return dispatcher->Run();
}
