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

#ifdef _DEBUG
#define TAOLOG_ENABLED
#define TAOLOG_METHOD_COPYDATA
#endif

#include "taolog.h"

#ifdef TAOLOG_ENABLED
// {DA72210D-0C17-4223-AA34-7EB7EDADB64F}
static const GUID providerGuid = 
{ 0xDA72210D, 0x0C17, 0x4223, { 0xAA, 0x34, 0x7E, 0xB7, 0xED, 0xAD, 0xB6, 0x4F } };
TaoLogger g_taoLogger(providerGuid);
#endif

#pragma comment(lib, "ws2_32")

static LPFN_ACCEPTEX s_fnAcceptEx;
static LPFN_GETACCEPTEXSOCKADDRS s_fnGetAcceptExSockAddrs;

static const size_t BUF_SIZE = 64 * 1024;
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

    // 操作失败（原因不是异步）
    bool Fail()     { return !_b && _e != WSA_IO_PENDING; }

    // 调用异步
    bool Async()    { return !_b && _e == WSA_IO_PENDING; }

    // 错误码
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

    WSARet GetResult(SOCKET fd, DWORD* pdwBytes, DWORD* pdwFlags = nullptr)
    {
        DWORD dwFlags = 0;
        WSABoolRet ret = ::WSAGetOverlappedResult(fd, &overlapped, pdwBytes, FALSE, &dwFlags);
        if(pdwFlags != nullptr)
            *pdwFlags = dwFlags;
        return ret;
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

    /*
    bool Closed(SOCKET fd)
    {
        DWORD dwBytes;
        WSARet ret = GetResult(fd, &dwBytes);
        return ret.Succ() && dwBytes == 0;
    }
    */

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
    struct Flags
    {
        enum Value
        {
            Closed  = 1 << 0,
        };
    };

    ClientSocketContext(SocketDispatcher* disp, SOCKET fd)
        : fd(fd)
        , _disp(disp)
        , flags(0)
    {

    }

    SOCKET fd;
    SOCKADDR_IN local;
    SOCKADDR_IN remote;
    typedef std::function<void(ClientSocketContext*, unsigned char*, size_t)> OnReadT;
    typedef std::function<void(ClientSocketContext*, size_t)> OnWrittenT;
    typedef std::function<void(ClientSocketContext*)> OnClosedT;
    OnReadT _onRead;
    OnWrittenT _onWritten;
    OnClosedT _onClosed;
    SocketDispatcher* _disp;
    DWORD flags;


    void Close()
    {
        flags |= Flags::Closed;
        WSAIntRet ret = closesocket(fd);
        LogLog("关闭client,fd=%d,ret=%d", fd, ret.Code());
        assert(ret.Succ());
    }

    void OnRead(OnReadT onRead)
    {
        _onRead = onRead;
    }

    void OnWritten(OnWrittenT onWritten)
    {
        _onWritten = onWritten;
    }

    void OnClosed(OnClosedT onClose)
    {
        _onClosed = onClose;
    }

    WSARet Write(const char* data, size_t size, void* tag)
    {
        return Write((const unsigned char*)data, size, tag);
    }

    WSARet Write(const char* data, void* tag)
    {
        return Write(data, std::strlen(data), tag);
    }

    WSARet Write(const unsigned char* data, size_t size, void* tag)
    {
        auto writeio = new WriteIOContext();
        auto ret = writeio->Write(fd, data, size);
        if(ret.Succ()) {
            LogLog("写立即成功，fd=%d,size=%d", fd, size);
        }
        else if(ret.Fail()) {
            LogFat("写错误：fd=%d,code=%d", fd, ret.Code());
        }
        else if(ret.Async()) {
            LogLog("写异步，fd=%d", fd);
        }
        return ret;
    }

    WSARet _Read()
    {
        auto readio = new ReadIOContext();
        auto ret = readio->Read(fd);
        if(ret.Succ()) {
            LogLog("_Read 立即成功, fd:%d", fd);
        }
        else if(ret.Fail()) {
            LogFat("读错误：fd:%d,code=%d", fd, ret.Code());
        }
        else if(ret.Async()) {
            LogLog("读异步 fd:%d", fd);
        }
        return ret;
    }

    void _OnRead(ReadIOContext* io, bool inThread)
    {
        /*
        if(inThread) {
            return _disp->Dispatch(this, static_cast<BaseIOContext*>(io));
        }
        */

        DWORD dwBytes = 0;
        WSARet ret = io->GetResult(fd, &dwBytes);
        if(ret.Succ() && dwBytes > 0) {
            LogLog("_OnRead 成功，fd:%d,dwBytes:%d", fd, dwBytes);
            _onRead(this, io->buf, dwBytes);
            _Read();
        }
        else {
            if(flags & Flags::Closed) {
                LogWrn("已主动关闭连接：fd:%d", fd);
            }
            else if(ret.Succ() && dwBytes == 0) {
                LogWrn("已被动关闭连接：fd:%d", fd);
                _onClosed(this);
            }
            else if(ret.Fail()) {
                LogFat("读失败：fd=%d,code:%d", fd, ret.Code());
            }
        }
    }

    WSARet _OnWritten(WriteIOContext* io, bool inThread)
    {
        /*
        if(inThread) {
            DWORD dwBytes = 0;
            WSARet ret = io->GetResult(fd, &dwBytes);
            if(ret.Fail()) {
                assert(0);
            }
            
            return _disp->Dispatch(this, static_cast<BaseIOContext*>(io));
        }
        */

        DWORD dwBytes = 0;
        WSARet ret = io->GetResult(fd, &dwBytes);
        if(ret.Succ() && dwBytes > 0) {
            _onWritten(this, dwBytes);
        }
        else {
            LogFat("写失败");
        }

        return ret;
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
                LogLog("_Accept 立即完成：client fd:%d, remote:%s", client->fd, to_string(client->remote).c_str());
            }
            else if(ret.Fail()) {
                LogFat("_Accept 错误：code=%d", ret.Code());
                assert(0);
            }
            else if(ret.Async()) {
                LogLog("_Accept 异步");
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
    WSARet ret = false;
    DWORD dwBytesTransfered;

    while(true) {
        BOOL bRet = GetQueuedCompletionStatus(hIocp, &dwBytesTransfered, (PULONG_PTR)&pBaseSocket, (LPOVERLAPPED*)&pIoContext, INFINITE);
        LogLog("收到完成端口事件：bRet=%d,dwBytes=%d", bRet, dwBytesTransfered);
        if(bRet == FALSE && dwBytesTransfered == 0 && pIoContext != nullptr && pIoContext->optype == OpType::Read) {
            continue;
        }

        if(pIoContext->optype == OpType::Accept) {
            pServerContext = static_cast<ServerSocketContext*>(pBaseSocket);
            acceptio = static_cast<AcceptIOContext*>(pIoContext);
            pClientSocket = pServerContext->_OnAccepted(acceptio);
            delete acceptio;
            LogLog("接收到客户端连接：fd:%d,local:%s,remote:%s",
                pClientSocket->fd,
                to_string(pClientSocket->local).c_str(),
                to_string(pClientSocket->remote).c_str()
            );
            pClientSocket->_Read();
            pServerContext->_Accept();
        }
        else if(pIoContext->optype == OpType::Read) {
            pClientSocket = static_cast<ClientSocketContext*>(pBaseSocket);
            readio = static_cast<ReadIOContext*>(pIoContext);
            LogLog("接收到读完成事件：fd:%d", pClientSocket->fd);
            pClientSocket->_OnRead(readio, true);
            delete readio;
        }
        else if(pIoContext->optype == OpType::Write) {
            pClientSocket = static_cast<ClientSocketContext*>(pBaseSocket);
            writeio = static_cast<WriteIOContext*>(pIoContext);
            LogLog("接收到写完成事件：fd:%d", pClientSocket->fd);
            pClientSocket->_OnWritten(writeio, true);
            delete writeio;
        }
        else if(pIoContext->optype == OpType::Init) {
            auto initio = static_cast<InitIOContext*>(pIoContext);
            delete initio;
            pServerContext = static_cast<ServerSocketContext*>(pBaseSocket);
            LogLog("接收到初始化事件，server fd:%d", pServerContext->fd);
            auto clients = pServerContext->_Accept();
            for(auto& client : clients) {
                client->_Read();
            }
        }
    }
}

int main()
{
#ifdef DEBUG
#ifdef TAOLOG_ENABLED 
    g_taoLogger.Init();
#endif // DEBUG
#endif

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    HANDLE hIocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    assert(hIocp != nullptr);
    LogLog("IOCP 句柄：%p", hIocp);

    SOCKET sckServer = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(sckServer != INVALID_SOCKET);
    LogLog("服务器fd=%d\n", sckServer);

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
    LogLog("监听地址：%s", to_string(addrServer).c_str());

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
        _beginthreadex(nullptr, 0, worker_thread, (void*)hIocp, 0, nullptr);
        LogLog("开启工作线程：%d", i + 1);
    }

    static const char* res =
        "HTTP/1.1 200 OK\r\n"
        "Server: iocp-demo\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 3\r\n"
        "Connection: close\r\n"
        "\r\n"
        "123"
        ;

    volatile long i = 0;

    ctx->OnAccepted([&i](ClientSocketContext* client) {
        LogLog("X客户端连接(i=%d)：%s,fd=%d\n", i, to_string(client->remote).c_str(), client->fd);
        client->OnRead([](ClientSocketContext* client, unsigned char* data, size_t size) {
            LogLog("接收到数据(fd=%d)：%.*s\n", client->fd, size, data);
            client->Write(res, nullptr);
        });
        client->OnWritten([](ClientSocketContext* client, size_t size) {
            LogLog("发送了数据(fd=%d)：%d 字节\n", client->fd, size);
        });
        client->OnClosed([&i](ClientSocketContext* client) {
            client->Close();
            InterlockedIncrement(&i);
            LogLog("已处理连接数：%d", i);
            std::printf("已处理连接数：%d\n", i);
        });
    });

    auto io = new InitIOContext();
    PostQueuedCompletionStatus(hIocp, 0, (ULONG_PTR)ctx, &io->overlapped);

    return dispatcher->Run();
}
