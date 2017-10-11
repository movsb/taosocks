#include <iostream>

#include <process.h>

#include "packet_manager.h"
#include "log.h"


namespace taosocks {
ClientPacketManager::ClientPacketManager(Dispatcher & disp)
    : _disp(disp)
    , _client(-1, disp)
    , _connected(false)
    , _seq(0)
{
    LogLog("创建客户端包管理器");
}

void ClientPacketManager::StartActive()
{
    ::UuidCreate(&_guid);

    LogLog("主动打开");

    ::_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);

    _client.OnConnect([this](ClientSocket*, bool connected) {
        LogLog("已连接到服务端");
        _connected = true;
    });

    _client.OnRead([this](ClientSocket* client, unsigned char* data, size_t size) {
        // LogLog("接收到数据 size=%d", size);
        return OnRead(client, data, size);
    });

    _client.OnWrite([](ClientSocket*, size_t size) {
        // LogLog("发送数据 size=%d", size);
    });

    _client.OnClose([this](ClientSocket*, CloseReason::Value reason){
        LogLog("与服务端断开连接");
    });

    in_addr addr;
    addr.S_un.S_addr = ::inet_addr("127.0.0.1");
    _client.Connect(addr, 8081);
}

void ClientPacketManager::Send(BasePacket* pkt)
{
    assert(pkt != nullptr);
    std::memcpy(&pkt->__guid, &_guid, sizeof(GUID));
    pkt->__seq = ++_seq;
    _packets.push_back(pkt);
    // LogLog("添加一个数据包");
}

void ClientPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data;
    recv_data.append(data, size);

    for(BasePacket* bpkt; (bpkt = recv_data.try_cast<BasePacket>()) != nullptr && (int)recv_data.size() >= bpkt->__size;) {
        LogLog("收到数据包 sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", bpkt->__sid, bpkt->__cid, bpkt->__seq, bpkt->__cmd, bpkt->__size);
        auto pkt = new (new char[bpkt->__size]) BasePacket;
        recv_data.get(pkt, bpkt->__size);
        auto handler = _handlers.find(pkt->__cid);
        if(handler == _handlers.cend()) {
            LogWrn("包没有处理器，丢弃。");
        }
        else {
            handler->second->OnPacket(pkt);
        }
    }
}

unsigned int ClientPacketManager::PacketThread()
{
    for(;;) {
        BasePacket* pkt =nullptr;

        if(_connected) {
            _lock.LockExecute([&] {
                if(!_packets.empty()) {
                    pkt = _packets.front();
                    _packets.pop_front();
                }
            });

        }

        if(pkt != nullptr) {
            LogLog("发送数据包 sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", pkt->__sid, pkt->__cid, pkt->__seq, pkt->__cmd, pkt->__size);
            _client.Write((char*)pkt, pkt->__size, nullptr);
        }
        else {
            ::Sleep(500);
        }
    }

    return 0;
}

unsigned int ClientPacketManager::__ThreadProc(void * that)
{
    return static_cast<ClientPacketManager*>(that)->PacketThread();
}

//////////////////////////////////////////////////////////////////////////

ServerPacketManager::ServerPacketManager(Dispatcher & disp)
    : _disp(disp)
    , _seq(0)
{
}

void ServerPacketManager::StartPassive()
{
    LogLog("被动打开");
    ::_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);
}

void ServerPacketManager::Send(BasePacket* pkt)
{
    assert(pkt != nullptr);
    pkt->__seq = ++_seq;
    _lock.LockExecute([&] {
        // LogLog("添加一个数据包");
        _packets.push_back(pkt);
    });
}

void ServerPacketManager::AddClient(ClientSocket* client)
{
    client->OnRead([this](ClientSocket* client, unsigned char* data, size_t size) {
        return OnRead(client, data, size);
    });

    client->OnWrite([](ClientSocket* client, size_t size) {
    });

    // 可能是掉线、可能是正常关闭
    // 掉线不清理网站连接，正常关闭要清理
    client->OnClose([this](ClientSocket*, CloseReason::Value reason) {
        if(reason == CloseReason::Actively) {
            LogLog("主动关闭连接，网站先断开");
            // 放在网站断开连接那儿去处理
        }
        else if(reason == CloseReason::Passively) {
            LogLog("被动关闭连接");

        }
        else if(reason == CloseReason::Reset) {
            LogLog("连接被重置");
        }
        else {
            LogWrn("Bad Reason");
        }
    });
}

void ServerPacketManager::RemoveClient(ClientSocket* client)
{

}

void ServerPacketManager::CloseLocal(const GUID & guid, int cid)
{
}

void ServerPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data[client];
    recv_data.append(data, size);

    for(BasePacket* bpkt; (bpkt = recv_data.try_cast<BasePacket>()) != nullptr && (int)recv_data.size() >= bpkt->__size;) {
        if(bpkt->__cmd == PacketCommand::Connect) {
            _clients.emplace(bpkt->__guid, client);
        }

        LogLog("收到数据包：sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", bpkt->__sid, bpkt->__cid, bpkt->__seq, bpkt->__cmd, bpkt->__size);

        auto pkt = new (new char[bpkt->__size]) BasePacket;
        recv_data.get(pkt, bpkt->__size);
        auto handler = _handlers.find(pkt->__sid);
        if(handler == _handlers.cend()) {
            LogWrn("包没有处理器，丢弃。");
        }
        else {
            handler->second->OnPacket(pkt);
        }
    }
}

unsigned int ServerPacketManager::PacketThread()
{
    for(;;) {
        BasePacket* pkt =nullptr;

            _lock.LockExecute([&] {
                if(!_packets.empty()) {
                    pkt = _packets.front();
                    _packets.pop_front();
                }
            });

        if(pkt != nullptr) {
            auto range = _clients.equal_range(pkt->__guid);
            if(range.first == range.second) {
                assert(0 && "没有接收端");
            }
            for(auto it = range.first; it != range.second; ++it) {
                auto client = it->second;
                client->Write((char*)pkt, pkt->__size, nullptr);
                LogLog("发送数据包：sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", pkt->__sid, pkt->__cid, pkt->__seq, pkt->__cmd, pkt->__size);
                break;
            }
        }
        else {
            ::Sleep(500);
        }
    }

    return 0;
}

unsigned int ServerPacketManager::__ThreadProc(void * that)
{
    return static_cast<ServerPacketManager*>(that)->PacketThread();
}

}