#include "client_socket.h"

#include "log.h"

namespace taosocks {
void ClientSocket::Close(bool force)
{
    if(!(_flags & Flags::Closed)) {
        if(!force && ((_flags & Flags::PendingRead) || _nPendingWrite > 0)) {
            LogWrn("有挂起的读/写操作，不能立即关闭。");
            _flags |= Flags::MarkClose;
        }
        else {
            _flags |= Flags::Closed;
            _flags &= ~Flags::MarkClose;
            _flags &= ~Flags::PendingRead;
            WSAIntRet ret = closesocket(_fd);
            LogLog("关闭client, ret=%d", ret.Code());
            assert(ret.Succ());
            if(_onClose) {
                _onClose(this, _close_reason);
            }
        }
    }
    else {
        LogWrn("早已关闭");
    }
}
void ClientSocket::OnRead(OnReadT onRead)
{
    _onRead = onRead;
}
void ClientSocket::OnWrite(OnWriteT onWrite)
{
    _onWrite = onWrite;
}
void ClientSocket::OnClose(OnCloseT onClose)
{
    _onClose = onClose;
}
void ClientSocket::OnConnect(OnConnectT onConnect)
{
    _onConnect = onConnect;
}
WSARet ClientSocket::Connect(in_addr& addr, unsigned short port)
{
    sockaddr_in sai = {0};
    sai.sin_family = PF_INET;
    sai.sin_addr.S_un.S_addr = INADDR_ANY;
    sai.sin_port = 0;
    WSAIntRet r = ::bind(_fd, (sockaddr*)&sai, sizeof(sai));
    assert(r.Succ());
    sai.sin_addr = addr;
    sai.sin_port = ::htons(port);

    auto connio = new ConnectIOContext();
    auto ret = connio->Connect(_fd, sai);
    if(ret.Succ()) {
        // LogLog("连接立即成功");
    }
    else if(ret.Fail()) {
        LogFat("连接调用失败：%d", ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("连接异步");
    }
    return ret;
}

WSARet ClientSocket::Write(const void* data, size_t size)
{
    auto writeio = new WriteIOContext();
    auto ret = writeio->Write(_fd, (const unsigned char*)data, size);
    if(ret.Succ()) {
        // DWORD dwBytes;
        // auto r = writeio->GetResult(_fd, &dwBytes);
        // assert(r && dwBytes == size);
        // LogLog("写立即成功，fd=%d,size=%d", _fd, size);
        _nPendingWrite++;
    }
    else if(ret.Fail()) {
        if(!(_flags & Flags::MarkClose)) {
            LogFat("写错误：code=%d", ret.Code());
        }
        _flags |= Flags::MarkClose;
        _CloseIfNeeded();
    }
    else if(ret.Async()) {
        _nPendingWrite++;
        // LogLog("写异步，fd=%d", _fd);
    }
    return ret;
}
WSARet ClientSocket::Read()
{
    assert(!IsClosed());
    if((_flags & Flags::PendingRead) != 0) {
        LogFat("不能多次读。");
        return WSABoolRet(FALSE);
    }

    _flags |= Flags::PendingRead;

    auto readio = new ReadIOContext();
    auto ret = readio->Read(_fd);
    if(ret.Succ()) {
        LogLog("_Read 立即成功, fd:%d", _fd);
    }
    else if(ret.Fail()) {
        if(!(_flags & Flags::MarkClose)) {
            LogFat("读错误：code=%d", ret.Code());
        }
        _flags &= ~Flags::PendingRead;
        _flags |= Flags::MarkClose;
        _CloseIfNeeded();
    }
    else if(ret.Async()) {
        LogLog("读异步 fd:%d", _fd);
    }
    return ret;
}

void ClientSocket::_CloseIfNeeded()
{
    if((_nPendingWrite <= 0)                // 写完了
        && !(_flags & Flags::PendingRead)   // 读完了
        && (_flags & Flags::MarkClose)      // 已被标记为关闭
        && (!IsClosed())                    // 尚未关闭
        )
    {
        Close(true);
    }
}

void ClientSocket::_OnRead(ReadIOContext* rio)
{
    _flags &= ~Flags::PendingRead;
    DWORD dwBytes = 0;
    WSARet ret = rio->GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {
        _onRead(this, rio->buf, dwBytes);
    }
    else {
        if(_flags & Flags::Closed) {
            LogWrn("已主动关闭连接");
            _OnReadFail(CloseReason::Actively);
        }
        else if(ret.Succ() && dwBytes == 0) {
            LogWrn("已被动关闭连接");
            _OnReadFail(CloseReason::Passively);
        }
        else if(ret.Fail()) {
            if(!(_flags & Flags::MarkClose)) {
                LogFat("读失败：code:%d", ret.Code());
            }
            _OnReadFail(CloseReason::Reset);
        }
    }
    delete rio;
}

void ClientSocket::_OnWrite(WriteIOContext* io)
{
    _nPendingWrite--;

    DWORD dwBytes = 0;
    WSARet ret = io->GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {
        assert(dwBytes == io->wsabuf.len);
        _onWrite(this, dwBytes);
    }
    else {
        if(!(_flags & Flags::MarkClose)) {
            LogFat("写失败 code=%d", ret.Code());
        }
        _flags |= Flags::MarkClose;
    }
    _CloseIfNeeded();
    delete io;
}

void ClientSocket::_OnReadFail(CloseReason reason)
{
    _close_reason = reason;
    _flags |= Flags::MarkClose;

    _CloseIfNeeded();
}

void ClientSocket::_OnConnect(ConnectIOContext* io)
{
    WSARet ret = io->GetResult(_fd);
    delete io;
    auto connected = ret.Succ();
    _flags &= ~Flags::Closed;
    _flags &= ~Flags::MarkClose;
    _flags &= Flags::PendingRead;
    _onConnect(this, connected);
}

void ClientSocket::OnTask(BaseIOContext* bio)
{
    switch(bio->optype) {
    case OpType::Read:
    {
        auto rio = static_cast<ReadIOContext*>(bio);
        if(_onRead == nullptr) {
            _read_queue.push_back(rio);
        }
        else {
            _OnRead(rio);
        }
        break;
    }
    case OpType::Write:
    {
        _OnWrite(static_cast<WriteIOContext*>(bio));
        break;
    }
    case OpType::Connect:
    {
        _OnConnect(static_cast<ConnectIOContext*>(bio));
        break;
    }
    }
}

}
