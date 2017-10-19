#include "client_socket.h"

#include "log.h"

namespace taosocks {
void ClientSocket::Close()
{
    if(!(_flags & Flags::Closed)) {
        _flags |= Flags::Closed;
        WSAIntRet ret = closesocket(_fd);
        LogLog("关闭client, id=%d,ret=%d", GetId(), ret.Code());
        assert(ret.Succ());
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
    }
    else if(ret.Fail()) {
        LogFat("写错误：id=%d,code=%d", GetId(), ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("写异步，fd=%d", _fd);
    }
    return ret;
}
WSARet ClientSocket::Read()
{
    assert(!IsClosed());
    assert((_flags & Flags::PendingRead) == 0);
    _flags |= Flags::PendingRead;

    auto readio = new ReadIOContext();
    auto ret = readio->Read(_fd);
    if(ret.Succ()) {
        // LogLog("_Read 立即成功, fd:%d", _fd);
    }
    else if(ret.Fail()) {
        LogFat("读错误：id=%d,code=%d", GetId(), ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("读异步 fd:%d", _fd);
    }
    return ret;
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
            LogWrn("已主动关闭连接：id=%d", GetId());
            _OnClose(CloseReason::Actively);
        }
        else if(ret.Succ() && dwBytes == 0) {
            LogWrn("已被动关闭连接：id:%d", GetId());
            _OnClose(CloseReason::Passively);
        }
        else if(ret.Fail()) {
            LogFat("读失败：id=%d,code:%d", GetId(), ret.Code());
            _OnClose(CloseReason::Reset);
        }
    }
}

void ClientSocket::_OnWrite(WriteIOContext* io)
{
    DWORD dwBytes = 0;
    WSARet ret = io->GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {
        assert(dwBytes == io->wsabuf.len);
        _onWrite(this, dwBytes);
    }
    else {
        LogFat("写失败 id=%d, code=%d", GetId(), ret.Code());
        LogLog("数据：%*s", io->wsabuf.len, io->wsabuf.buf);
    }
}

void ClientSocket::_OnClose(CloseReason reason)
{
    if(reason == CloseReason::Passively || reason == CloseReason::Reset) {
        Close();
    }
    assert(_onClose);
    _onClose(this, reason);
}

void ClientSocket::_OnConnect(ConnectIOContext* io)
{
    WSARet ret = io->GetResult(_fd);
    auto connected = ret.Succ();
    _flags &= ~Flags::Closed;
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
