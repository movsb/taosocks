#pragma once

#include <cassert>

#include <functional>

#include <Windows.h>

namespace taosocks {
namespace threading {

class Locker
{
public:
    Locker()
    {
        ::InitializeCriticalSection(&_cs);
    }
    ~Locker()
    {
        ::DeleteCriticalSection(&_cs);
    }

    void Lock()
    {
        ::EnterCriticalSection(&_cs);
    }

    void Unlock()
    {
        ::LeaveCriticalSection(&_cs);
    }

    void LockExecute(std::function<void()> callable)
    {
        Lock();
        callable();
        Unlock();
    }

private:
    CRITICAL_SECTION _cs;
};

struct IDispatcher
{
    virtual void OnDispatch(void* data) = 0;
};

class Dispatcher
{
public:
    Dispatcher();

public:
    int Run();
    void Dispatch(IDispatcher* disp, void* data);

protected:
    void _Create();

protected:
    LRESULT WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT __stdcall __WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:
    HWND _hwnd;
    Locker _lock;
};

}

using threading::Dispatcher;

}
