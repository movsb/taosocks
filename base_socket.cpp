#include "base_socket.h"

namespace taosocks {
namespace base_socket {

void BaseSocket::CreateSocket()
{
    _fd = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(_fd != INVALID_SOCKET);
}

void BaseSocket::Dispatch(BaseDispatchData & data)
{
    return _disp.Dispatch(this, &data);
}

void BaseSocket::OnDispatch(void * data)
{
    auto base = static_cast<BaseDispatchData*>(data);
    return OnDispatch(*base);
}

void BaseSocket::OnTask(OVERLAPPED* overlapped)
{
    auto io = reinterpret_cast<BaseIOContext*>(overlapped);
    return OnTask(*io);
}

HANDLE BaseSocket::GetHandle()
{
    return reinterpret_cast<HANDLE>(GetSocket());
}

WSARet BaseIOContext::GetResult(SOCKET fd, DWORD* pdwBytes /*= nullptr*/, DWORD* pdwFlags /*= nullptr*/)
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

}
}
