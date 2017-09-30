#include <iostream>

#include <process.h>

#include "packet_manager.h"
#include "log.h"


namespace taosocks {
ClientPacketManager::ClientPacketManager(Dispatcher & disp)
    : _disp(disp)
    , _client(disp)
    , _connected(false)
{
    LogLog("创建客户端包管理器：fd=%d\n", _client.GetDescriptor());
}

void ClientPacketManager::StartActive()
{
    ::UuidCreate(&_guid);

    LogLog("主动打开");

    ::_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);

    _client.OnConnected([this](ClientSocket*) {
        LogLog("已连接到服务端");
        _connected = true;
    });

    _client.OnRead([this](ClientSocket* client, unsigned char* data, size_t size) {
        LogLog("接收到数据 size=%d", size);
        return OnRead(client, data, size);
    });

    _client.OnWritten([](ClientSocket*, size_t size) {
        LogLog("发送数据 size=%d", size);
    });

    _client.OnClosed([this](ClientSocket*){
        LogLog("连接已关闭");
    });

    in_addr addr;
    addr.S_un.S_addr = ::inet_addr("127.0.0.1");
    _client.Connect(addr, 8081);
}

void ClientPacketManager::Send(BasePacket* pkt)
{
    assert(pkt != nullptr);
    std::memcpy(&pkt->__guid, &_guid, sizeof(GUID));
    _packets.push_back(pkt);
    LogLog("添加一个数据包");
}

void ClientPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data;

    recv_data.insert(recv_data.cend(), data, data + size);
    if(recv_data.size() >= sizeof(BasePacket)) {
        auto bpkt = (BasePacket*)recv_data.data();
        if((int)recv_data.size() >= bpkt->__size) {
            LogLog("接收到一个数据包 cmd=%d", bpkt->__cmd);
            auto pkt = new (new unsigned char[bpkt->__size]) BasePacket;
            std::memcpy(pkt, recv_data.data(), bpkt->__size);
            recv_data.erase(recv_data.cbegin(), recv_data.cbegin() + bpkt->__size);
            auto handler = _handlers.find(pkt->__cfd);
            assert(handler != _handlers.cend());
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
            LogLog("尝试写入一个数据包, cmd=%d", pkt->__cmd);
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
    _lock.LockExecute([&] {
        LogLog("添加一个数据包");
        _packets.push_back(pkt);
    });
}

void ServerPacketManager::AddClient(ClientSocket* client)
{
    client->OnRead([this](ClientSocket* client, unsigned char* data, size_t size) {
        return OnRead(client, data, size);
    });

    client->OnWritten([](ClientSocket* client, size_t size) {
    });
}

void ServerPacketManager::RemoveClient(ClientSocket* client)
{

}

void ServerPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data[client];

    recv_data.insert(recv_data.cend(), data, data + size);
    if(recv_data.size() >= sizeof(BasePacket)) {
        auto bpkt = (BasePacket*)recv_data.data();
        if(bpkt->__cmd == PacketCommand::ResolveAndConnect) {
            AddClient(client);
            _clients.emplace(bpkt->__guid, client);
        }
        if((int)recv_data.size() >= bpkt->__size) {
            LogLog("收到一个数据包，来自fd=%d, cmd=%d", client->GetDescriptor(), bpkt->__cmd);
            auto pkt = new (new unsigned char[bpkt->__size]) BasePacket;
            std::memcpy(pkt, recv_data.data(), bpkt->__size);
            recv_data.erase(recv_data.cbegin(), recv_data.cbegin() + bpkt->__size);
            auto handler = _handlers.find(pkt->__sfd);
            assert(handler != _handlers.cend());
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
                LogLog("发送一个数据包：fd=%d, cmd=%d", client->GetDescriptor(), pkt->__cmd);
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