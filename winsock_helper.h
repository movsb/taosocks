#pragma once

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

    bool Succ()     { return _b; }

    // 操作失败（原因不是异步）
    bool Fail()     { return !_b && _e != WSA_IO_PENDING; }

    // 调用异步
    bool Async()    { return !_b && _e == WSA_IO_PENDING; }

    // 错误码
    int Code()      { return _e; }

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
    { }
};

class WSABoolRet : public WSARet
{
public:
    WSABoolRet(BOOL value)
        : WSARet(value != FALSE)
    { }
};

class WSA
{
public:
    void Startup();
    void Shutdown();

protected:
    void _Init();

public:
    static LPFN_ACCEPTEX                AcceptEx;
    static LPFN_GETACCEPTEXSOCKADDRS    GetAcceptExSockAddrs;
    static LPFN_CONNECTEX               ConnectEx;
};


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
        auto port = ::ntohs(inaddr->sin_port);

        return inaddr->sin_addr.S_un.S_addr;
	}

protected:
	int _size;
	struct addrinfo* _paddr;
};


}
}
