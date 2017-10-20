#pragma once

#include <cassert>

#include <string>

#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>
#include <Windows.h>

namespace taosocks {
namespace winsock {

std::string to_string(const SOCKADDR_IN& r);

class WSARet
{
public:
    WSARet(bool b)
        : _b(b)
    {
        _e = ::WSAGetLastError();
    }

    bool Succ() { return _b; }

    // 操作失败（原因不是异步）
    bool Fail() { return !_b && _e != WSA_IO_PENDING; }

    // 调用异步
    bool Async() { return !_b && _e == WSA_IO_PENDING; }

    // 错误码
    int Code() { return _e; }

    operator bool() { return Succ(); }

private:
    bool _b;
    int _e;
};

class WSAIntRet : public WSARet
{
public:
    WSAIntRet(int value)
        : WSARet(value == 0)
    {
    }
};

class WSABoolRet : public WSARet
{
public:
    WSABoolRet(BOOL value)
        : WSARet(value != FALSE)
    {
    }
};

class WSA
{
public:
    static void Startup();
    static void Shutdown();

protected:
    static void _Init();

public:
    static LPFN_ACCEPTEX                AcceptEx;
    static LPFN_GETACCEPTEXSOCKADDRS    GetAcceptExSockAddrs;
    static LPFN_CONNECTEX               ConnectEx;
};
}
using namespace winsock;

}
