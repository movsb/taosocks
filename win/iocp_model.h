#pragma once

#include <cassert>

#include <Windows.h>

namespace taosocks {
namespace iocp {

struct ITaskHandler
{
    virtual void OnTask(OVERLAPPED* overlapped) = 0;
    virtual HANDLE GetHandle() = 0;
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
    void Attach(ITaskHandler* handler);
    void PostStatus(const ITaskHandler& handler, const OVERLAPPED& overlapped);

protected:
    bool GetStatus(ITaskHandler** handler, OVERLAPPED** overlapped);

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

using iocp::IOCP;

}
