#pragma once

#include <cassert>

#include <Windows.h>

namespace taosocks {
namespace threading {

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
};

}

using threading::Dispatcher;

}
