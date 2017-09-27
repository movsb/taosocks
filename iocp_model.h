#pragma once

#include <cassert>

#include <Windows.h>

namespace taosocks {
namespace iocp {

struct TaskHandler
{
    virtual void Handle(OVERLAPPED* overlapped) = 0;
};

class IOCP
{
public:
    IOCP()
        : _handle(nullptr)
    {
        _Create();
    }

    ~IOCP()
    {
        _Close();
    }


public:
    void Attach(SOCKET fd, TaskHandler* handler);
    void PostStatus(TaskHandler* handler, const OVERLAPPED& overlapped);
    bool GetStatus(TaskHandler** handler, OVERLAPPED** overlapped);

protected:
    void _Create();
    void _Close();

protected:
    unsigned int WorkThread();
    static unsigned int __stdcall __ThreadProc(void* tag);

protected:
    HANDLE _handle;
};

}
}