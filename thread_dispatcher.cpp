#include "thread_dispatcher.h"

namespace taosocks {
namespace threading {

const UINT s_msg = WM_USER + 1;
const char* const s_cls = "{2FF09706-39AD-4FA0-B137-C4416E39C973}";

Dispatcher::Dispatcher()
{
    _Create();
}

int Dispatcher::Run()
{
    MSG msg;
    while(::GetMessage(&msg, nullptr, 0, 0)) {
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void Dispatcher::Dispatch(IDispatcher * disp, void * data)
{
    assert(::IsWindow(_hwnd));
    ::PostMessage(_hwnd, s_msg, WPARAM(disp), LPARAM(data));
}

void Dispatcher::_Create()
{
    WNDCLASSEX wc = {sizeof(wc)};
    wc.hInstance = ::GetModuleHandle(nullptr);
    wc.lpfnWndProc = __WndProc;
    wc.lpszClassName = s_cls;
    wc.cbWndExtra = sizeof(void*);
    ::RegisterClassEx(&wc);

    _hwnd = ::CreateWindowEx(
        0, s_cls, nullptr, 0, 
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, 
        nullptr,
        this
    );

    assert(_hwnd != nullptr);
}

LRESULT Dispatcher::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if(uMsg == s_msg) {
        auto pDisp = reinterpret_cast<IDispatcher*>(wParam);
        auto pData = reinterpret_cast<void*>(lParam);
        pDisp->OnDispatch(pData);
        return 0;
    }

    return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT Dispatcher::__WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    auto pThis = (Dispatcher*)::GetWindowLongPtr(hWnd, 0);

    switch(uMsg)
    {
    case WM_NCCREATE:
        pThis = (Dispatcher*)LPCREATESTRUCT(lParam)->lpCreateParams;
        ::SetWindowLongPtr(hWnd, 0, (LONG)pThis);
        break;
    case WM_NCDESTROY:
        pThis = nullptr;
        ::SetWindowLongPtr(hWnd, 0, 0);
        break;
    }

    return pThis
        ? pThis->WndProc(hWnd, uMsg, wParam, lParam)
        : ::DefWindowProc(hWnd, uMsg, wParam, lParam)
        ;
}

}
}
