#include "base_socket.h"

namespace taosocks {
namespace base_socket {

void BaseSocket::CreateSocket()
{
    assert(_fd == INVALID_SOCKET);
    _fd = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    assert(_fd != INVALID_SOCKET);
    extern IOCP* g_ios;
    g_ios->Attach(this);
}

void BaseSocket::OnDispatch(void* data)
{
    return OnTask(static_cast<BaseIOContext*>(data));
}

void BaseSocket::OnTask(OVERLAPPED* overlapped)
{
    assert(offsetof(BaseIOContext, overlapped) == 0);
    extern Dispatcher* g_disp;
    return g_disp->Dispatch(this, reinterpret_cast<BaseIOContext*>(overlapped));
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
