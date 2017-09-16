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
#include <algorithm>
#include <ws2tcpip.h>

#ifdef TAOLOG_ENABLED
// {DA72210D-0C17-4223-AA34-7EB7EDADB64F}
static const GUID providerGuid = 
{ 0xDA72210D, 0x0C17, 0x4223, { 0xAA, 0x34, 0x7E, 0xB7, 0xED, 0xAD, 0xB6, 0x4F } };
TaoLogger g_taoLogger(providerGuid);
#endif

#pragma comment(lib, "ws2_32")

static HANDLE g_hiocp;

static LPFN_ACCEPTEX s_fnAcceptEx;
static LPFN_GETACCEPTEXSOCKADDRS s_fnGetAcceptExSockAddrs;
static LPFN_CONNECTEX s_fnConnectEx;

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
        Close,
        Accept,
        Read,
        Write,
        Connect,
    };
};

struct BaseDispatchData
{
    BaseDispatchData(OpType::Value type)
        : optype(type)
    { }

    OpType::Value optype;
};

struct ISocketDispatcher
{
    virtual void Invoke(BaseDispatchData* data) = 0;
};

struct SocketDispatcher
{
    HWND hwnd;
    static const UINT msg = WM_USER + 0;

    SocketDispatcher()
    {
        Init();
    }

    void Dispatch(ISocketDispatcher* disp, BaseDispatchData* data)
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
            auto pData = reinterpret_cast<BaseDispatchData*>(lParam);
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

    WSARet GetResult(SOCKET fd, DWORD* pdwBytes = nullptr, DWORD* pdwFlags = nullptr)
    {
        DWORD dwFlags = 0;
        DWORD dwBytes = 0;
        WSABoolRet ret = ::WSAGetOverlappedResult(fd, &overlapped, &dwBytes, FALSE, &dwFlags);
        if(pdwBytes != nullptr)
            *pdwBytes = dwBytes;
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

    WSABUF wsabuf;
    unsigned char buf[BUF_SIZE];
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
        WSABoolRet R = s_fnConnectEx(
            fd, 
            (sockaddr*)&addr, sizeof(addr), 
            nullptr, 0,
            nullptr,
            &overlapped
        );

        return R;
    }
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

    struct ConnectDispatchData : BaseDispatchData
    {
        ConnectDispatchData()
            : BaseDispatchData(OpType::Connect)
        { }
    };

    struct ReadDispatchData : BaseDispatchData
    {
        ReadDispatchData()
            : BaseDispatchData(OpType::Read)
        { }

        unsigned char* data;
        size_t size;
    };

    struct WriteDispatchData : BaseDispatchData
    {
        WriteDispatchData()
            : BaseDispatchData(OpType::Write)
        { }

        size_t size;
    };

    struct CloseDispatchData : BaseDispatchData
    {
        CloseDispatchData()
            : BaseDispatchData(OpType::Close)
        { }
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
    typedef std::function<void(ClientSocketContext*)> OnConnectedT;
    OnReadT _onRead;
    OnWrittenT _onWritten;
    OnClosedT _onClosed;
    OnConnectedT _onConnected;
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

    void OnConnected(OnConnectedT onConnected)
    {
        _onConnected = onConnected;
    }

    WSARet Connect(const sockaddr_in& addr)
    {
        auto connio = new ConnectIOContext();
        auto ret = connio->Connect(fd, addr);
        if(ret.Succ()) {
            LogLog("连接立即成功");
        }
        else if(ret.Fail()) {
            LogFat("连接调用失败：%d", ret.Code());
        }
        else if(ret.Async()) {
            LogLog("连接异步");
        }
        return ret;
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

    void _OnRead(ReadIOContext* io)
    {
        DWORD dwBytes = 0;
        WSARet ret = io->GetResult(fd, &dwBytes);
        if(ret.Succ() && dwBytes > 0) {
            LogLog("_OnRead 成功，fd:%d,dwBytes:%d", fd, dwBytes);
            ReadDispatchData data;
            data.data = io->buf;
            data.size = dwBytes;
            _disp->Dispatch(this, &data);
            _Read();
        }
        else {
            if(flags & Flags::Closed) {
                LogWrn("已主动关闭连接：fd:%d", fd);
            }
            else if(ret.Succ() && dwBytes == 0) {
                LogWrn("已被动关闭连接：fd:%d", fd);
                CloseDispatchData data;
                _disp->Dispatch(this, &data);
            }
            else if(ret.Fail()) {
                LogFat("读失败：fd=%d,code:%d", fd, ret.Code());
            }
        }
    }

    WSARet _OnWritten(WriteIOContext* io)
    {
        DWORD dwBytes = 0;
        WSARet ret = io->GetResult(fd, &dwBytes);
        if(ret.Succ() && dwBytes > 0) {
            WriteDispatchData data;
            data.size = dwBytes;
            _disp->Dispatch(this, &data);
        }
        else {
            LogFat("写失败");
        }

        return ret;
    }

    WSARet _OnConnected(ConnectIOContext* io)
    {
        WSARet ret = io->GetResult(fd);
        if(ret.Succ()) {
            ConnectDispatchData data;
            _disp->Dispatch(this, &data);
        }
        else {
            LogFat("连接失败");
        }

        return ret;
    }

    virtual void Invoke(BaseDispatchData* data) override
    {
        switch(data->optype)
        {
        case OpType::Read:
        {
            auto d = static_cast<ReadDispatchData*>(data);
            _onRead(this, d->data, d->size);
            break;
        }
        case OpType::Write:
        {
            auto d = static_cast<WriteDispatchData*>(data);
            _onWritten(this, d->size);
            break;
        }
        case OpType::Close:
        {
            auto d = static_cast<CloseDispatchData*>(data);
            _onClosed(this);
            break;
        }
        case OpType::Connect:
        {
            auto d = static_cast<ConnectDispatchData*>(data);
            _onConnected(this);
            break;
        }
        }
    }
};

struct ServerSocketContext : public BaseSocketContext, public ISocketDispatcher
{
    HANDLE hiocp;
    SOCKET fd;
    HWND hwnd;
    std::function<void(ClientSocketContext*)> _onAccepted;
    SocketDispatcher* _disp;

    struct AcceptDispatchData : BaseDispatchData
    {
        AcceptDispatchData()
            : BaseDispatchData(OpType::Accept)
        { }

        ClientSocketContext* client;
    };

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
        AcceptDispatchData data;
        data.client = client;
        _disp->Dispatch(this, &data);
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

    virtual void Invoke(BaseDispatchData* data) override
    {
        switch(data->optype)
        {
        case OpType::Accept:
        {
            auto d = static_cast<AcceptDispatchData*>(data);
            _onAccepted(d->client);
            break;
        }
        }
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
    ConnectIOContext* connio;
    WSARet ret = false;
    DWORD dwBytesTransfered;

    while(true) {
        BOOL bRet = GetQueuedCompletionStatus(hIocp, &dwBytesTransfered, (PULONG_PTR)&pBaseSocket, (LPOVERLAPPED*)&pIoContext, INFINITE);

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
        else if(pIoContext->optype == OpType::Connect) {
            pClientSocket = static_cast<ClientSocketContext*>(pBaseSocket);
            connio = static_cast<ConnectIOContext*>(pIoContext);
            pClientSocket->_OnConnected(connio);
        }
        else if(pIoContext->optype == OpType::Read) {
            pClientSocket = static_cast<ClientSocketContext*>(pBaseSocket);
            readio = static_cast<ReadIOContext*>(pIoContext);
            LogLog("接收到读完成事件：fd:%d", pClientSocket->fd);
            pClientSocket->_OnRead(readio);
            delete readio;
        }
        else if(pIoContext->optype == OpType::Write) {
            pClientSocket = static_cast<ClientSocketContext*>(pBaseSocket);
            writeio = static_cast<WriteIOContext*>(pIoContext);
            LogLog("接收到写完成事件：fd:%d", pClientSocket->fd);
            pClientSocket->_OnWritten(writeio);
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

namespace SocksProxy
{


class resolver{
public:
	resolver()
		: _size(0)
		, _paddr(nullptr)
	{}

	~resolver(){
		free();
	}

    bool resolve(const std::string& host, const std::string& service)
    {
        struct addrinfo hints;
		struct addrinfo* pres = nullptr;
		int res;

		free();

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		res = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &pres);
		if (res != 0){
			_paddr = nullptr;
			_size = 0;
            return false;
		}

		_paddr = pres;
		while (pres){
			_size++;
			pres = pres->ai_next;
		}

		return true;
    }

	void free() {
		_size = 0;
		if (_paddr){
			::freeaddrinfo(_paddr);
			_paddr = nullptr;
		}
	}

	size_t size() const {
		return _size;
	}

    unsigned int operator[](int index) {
		struct addrinfo* p = _paddr;
		while (index > 1){
			p = p->ai_next;
			index--;
		}

        auto inaddr = (sockaddr_in*)p->ai_addr;
        auto ip = (std::string)::inet_ntoa(inaddr->sin_addr);
        auto port = ::ntohs(inaddr->sin_port);

        return inaddr->sin_addr.S_un.S_addr;
	}

protected:
	int _size;
	struct addrinfo* _paddr;
};

struct SocksVersion
{
    enum Value {
        v4 = 0x04,
        v5 = 0x05,
    };
};

struct SocksCommand
{
    enum Value {
        Stream      = 0x01,
    };
};

struct ConnectionStatus
{
    enum Value {
        Success     = 0x5A,
        Fail        = 0x5B,
    };
};

class SocksServer
{
private:
    struct Phrase
    {
        enum Value {
            Init,
            Command,
            Port,
            Addr,
            Domain,
            User,
        };
    };

public:
    SocksServer(ClientSocketContext* client)
        : _client(client)
        , _phrase(Phrase::Init)
        , _i(0)
    {
        _client->OnRead([&](ClientSocketContext*, unsigned char* data, size_t size) {
            feed(data, size);
        });

        _i = 0;

        _client->OnWritten([&](ClientSocketContext*, size_t size) {
            _i += size;
            if(_i == 8) {
                _client->OnWritten([&](ClientSocketContext*, size_t size) {
                    std::cout << "发送给浏览器 " << _client->fd << ": "  << size << std::endl;
                    if(size > _send.size()) {
                        // assert("fail" && 0);
                        size = _send.size();
                    }
                    _send.erase(_send.cbegin(), _send.cbegin() + size);
                    if(!_send.empty()) {
                        _client->Write(_send.data(), _send.size(), nullptr);
                    }
                });
            }
            _send.erase(_send.cbegin(), _send.cbegin() + size);
            if(!_send.empty()) {
                _client->Write(_send.data(), _send.size(), nullptr);
            }
        });

        _client->OnClosed([&](ClientSocketContext*) {
            //delete this;
        });
    }

public:
    void feed(const unsigned char* data, size_t size)
    {
        _recv.insert(_recv.cend(), data, data + size);

        auto& D = _recv;

        while(!D.empty()) {
            switch(_phrase) {
            case Phrase::Init:
            {
                auto ver = (SocksVersion::Value)D[0];
                D.erase(D.begin());

                if(ver != SocksVersion::v4) {
                    assert(0);
                }

                _phrase = Phrase::Command;
                break;
            }
            case Phrase::Command:
            {
                auto cmd = (SocksCommand::Value)D[0];
                D.erase(D.begin());

                if(cmd != SocksCommand::Stream) {
                    assert(0);
                }

                _phrase = Phrase::Port;
                break;
            }
            case Phrase::Port:
            {
                if(D.size() < 2) {
                    return;
                }

                unsigned short port_net = D[0] + (D[1] << 8);
                D.erase(D.begin(), D.begin() + 2);
                _port = ::ntohs(port_net);

                _phrase = Phrase::Addr;
                break;
            }
            case Phrase::Addr:
            {
                if(D.size() < 4) {
                    return;
                }

                _addr.S_un.S_un_b.s_b1 = D[0];
                _addr.S_un.S_un_b.s_b2 = D[1];
                _addr.S_un.S_un_b.s_b3 = D[2];
                _addr.S_un.S_un_b.s_b4 = D[3];
                D.erase(D.begin(), D.begin() + 4);

                _phrase = Phrase::User;
                break;
            }
            case Phrase::User:
            {
                auto term = std::find_if(D.cbegin(), D.cend(), [](const unsigned char& c) {
                    return c == '\0';
                });

                if(term == D.cend()) {
                    return;
                }

                D.erase(D.begin(), term + 1);
                _phrase = Phrase::Domain;
                break;
            }
            case Phrase::Domain:
            {
                auto term = std::find_if(D.cbegin(), D.cend(), [](const unsigned char& c) {
                    return c == '\0';
                });

                if(term == D.cend()) {
                    return;
                }

                _domain = (char*)&D[0];

                D.erase(D.begin(), term + 1);

                resolver rsv;
                if(!rsv.resolve(_domain, std::to_string(_port))) {
                    assert(0);
                }

                SOCKET fd = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
                assert(fd != INVALID_SOCKET);

                auto c = new ClientSocketContext(_client->_disp, fd);
                ::CreateIoCompletionPort((HANDLE)fd, g_hiocp, (ULONG_PTR)static_cast<BaseSocketContext*>(c), 0);

                auto remote_addr = rsv[0];

                c->OnConnected([&,c](ClientSocketContext*) {
                    std::cout << "Connected to " << _domain << std::endl;

                    _client->OnRead([&,c](ClientSocketContext*, unsigned char* data, size_t size) {
                        std::cout << "收到浏览器 " << _client->fd << ":" <<  size << std::endl;
                       _recv.insert(_recv.cend(), data, data + size);
                       c->Write(_recv.data(), _recv.size(), nullptr);
                    });

                    _send.push_back(0x00);
                    _send.push_back(ConnectionStatus::Success);
                    _send.push_back(_port >> 8);
                    _send.push_back(_port & 0xff);

                    //auto addr = ::htonl(remote_addr);
                    auto addr = remote_addr;
                    char* a = (char*)&addr;
                    _send.push_back(a[0]);
                    _send.push_back(a[1]);
                    _send.push_back(a[2]);
                    _send.push_back(a[3]);

                    _client->Write(_send.data(), _send.size(), nullptr);

                    c->OnRead([this](ClientSocketContext*, unsigned char* data, size_t size) {
                        std::cout << "收到服务器 "<< _client->fd << ":"  << size << std::endl;
                        _send.insert(_send.cend(), data, data + size);
                        _client->Write(_send.data(), _send.size(), nullptr);
                    });

                    c->OnWritten([this](ClientSocketContext*, size_t size) {
                        std::cout << "发送给服务器 " << _client->fd << ":" << size << std::endl;
                        _recv.erase(_recv.cbegin(), _recv.cbegin() + size);
                    });

                    c->OnClosed([this](ClientSocketContext*) {
                        _client->Close();
                    });

                    c->_Read();
                });

                sockaddr_in sai = {0};
                sai.sin_family = PF_INET;
                sai.sin_addr.S_un.S_addr = INADDR_ANY;
                sai.sin_port = 0;
                WSAIntRet r = ::bind(fd, (sockaddr*)&sai, sizeof(sai));
                assert(r.Succ());
                sai.sin_addr.S_un.S_addr = rsv[0];
                sai.sin_port = ::htons(_port);
                c->Connect(sai);

                break;
            }
            }
        }
    }

protected:
    int _i;
    Phrase::Value _phrase;
    ClientSocketContext* _client;
    std::vector<unsigned char> _send;
    std::vector<unsigned char> _recv;
    unsigned short _port;
    in_addr _addr;
    std::string _domain;
};

}

int main()
{
#ifdef _DEBUG
#ifdef TAOLOG_ENABLED 
    g_taoLogger.Init();
#endif // DEBUG
#endif

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    HANDLE hIocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    assert(hIocp != nullptr);
    LogLog("IOCP 句柄：%p", hIocp);

    g_hiocp = hIocp;

    SOCKET sckServer = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(sckServer != INVALID_SOCKET);
    LogLog("服务器fd=%d\n", sckServer);

    auto dispatcher = new SocketDispatcher();

    auto pServer = new ServerSocketContext(dispatcher);
    pServer->hiocp = hIocp;
    pServer->fd = sckServer;
    hIocp = ::CreateIoCompletionPort((HANDLE)sckServer, hIocp, (ULONG_PTR)static_cast<BaseSocketContext*>(pServer), 0);
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
    GUID guidConnectEx = WSAID_CONNECTEX;

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

    WSAIoctl(
        sckServer,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidConnectEx, sizeof(guidConnectEx),
        &s_fnConnectEx, sizeof(s_fnConnectEx),
        &dwBytes,
        nullptr, nullptr
    );


    assert(s_fnAcceptEx != nullptr);
    assert(s_fnGetAcceptExSockAddrs != nullptr);
    assert(s_fnConnectEx != nullptr);


    for(auto i = 0; i < 1; i++) {
        _beginthreadex(nullptr, 0, worker_thread, (void*)hIocp, 0, nullptr);
        LogLog("开启工作线程：%d", i + 1);
    }

    pServer->OnAccepted([](ClientSocketContext* client) {
        new SocksProxy::SocksServer(client);
    });

    auto io = new InitIOContext();
    PostQueuedCompletionStatus(hIocp, 0, (ULONG_PTR)static_cast<BaseSocketContext*>(pServer), &io->overlapped);

    return dispatcher->Run();
}
