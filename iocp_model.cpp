#include <process.h>

#include "iocp_model.h"

namespace taosocks {
namespace iocp {

void IOCP::Attach(TaskHandler* handler)
{
    auto fd = reinterpret_cast<HANDLE>(handler->GetDescriptor());
    assert(fd != nullptr && fd != INVALID_HANDLE_VALUE);
    _handle = ::CreateIoCompletionPort(fd, _handle, ULONG_PTR(handler), 0);
    assert(_handle != nullptr);
}

void IOCP::PostStatus(const TaskHandler& handler, const OVERLAPPED& overlapped)
{
    ::PostQueuedCompletionStatus(_handle, 0, ULONG_PTR(&handler), const_cast<OVERLAPPED*>(&overlapped));
}

bool IOCP::GetStatus(TaskHandler** handler, OVERLAPPED** overlapped)
{
    DWORD dwBytes;
    BOOL bRet;

    bRet = ::GetQueuedCompletionStatus(_handle, &dwBytes,
        reinterpret_cast<PULONG_PTR>(handler),
        overlapped,
        INFINITE
    );

    // assert(bRet != FALSE);
    // return !!bRet;
    return true;
}

void IOCP::_Create()
{
    _handle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    assert(_handle != nullptr);

    for(int i = 0; i < 10; i++) {
        HANDLE handle = (HANDLE)_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);
        ::CloseHandle(handle);
    }
}

void IOCP::_Close()
{
    if(_handle != nullptr) {
        ::CloseHandle(_handle);
        _handle = nullptr;
    }
}

unsigned int IOCP::WorkThread()
{
    TaskHandler* handler;
    OVERLAPPED* overlapped;

    while(GetStatus(&handler, &overlapped)) {
        handler->OnTask(overlapped);
    }

    return 0;
}

unsigned int IOCP::__ThreadProc(void* tag)
{
    return static_cast<IOCP*>(tag)->WorkThread();
}

}
}