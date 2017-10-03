#pragma once

#include <list>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

#include "client_socket.h"
#include "thread_dispatcher.h"

namespace taosocks {
namespace packet_manager {

struct Command {
    enum Value {
        __Beg,
        Connect,
        Disconnect,
        Relay,
        __End,
    };
};
}

typedef packet_manager::Command PacketCommand;

#pragma pack(push, 1)

struct  BasePacket
{
    int  __size;
    GUID __guid;
    int  __cmd;
    int  __sfd;
    int  __cfd;
};

struct RelayPacket : BasePacket
{
    unsigned char data[1];

    static RelayPacket* Create(SOCKET sfd, SOCKET cfd, const unsigned char* data, size_t size)
    {
        auto pkt_size = sizeof(BasePacket) + size;
        auto p = new (new char[pkt_size]) RelayPacket;
        p->__size = pkt_size;
        p->__cmd = PacketCommand::Relay;
        p->__sfd = sfd;
        p->__cfd = cfd;
        std::memcpy(p->data, data, size);
        return p;
    }
};

struct DisconnectPacket : BasePacket
{

};

#pragma pack(pop)

struct IPacketHandler
{
    virtual int GetDescriptor() = 0;
    virtual void OnPacket(BasePacket* packet) = 0;
};

class ClientPacketManager
{
public:
    ClientPacketManager(Dispatcher& disp);

    void StartActive();

    ClientSocket& GetClient() { return _client; }

    void Send(BasePacket* pkt);

    void AddHandler(IPacketHandler* handler)
    {
        assert(_handlers.find(handler->GetDescriptor()) == _handlers.cend());
        _handlers[handler->GetDescriptor()] = handler;
    }

    void RemoveHandler(IPacketHandler* handler)
    {
        assert(_handlers.find(handler->GetDescriptor()) != _handlers.cend());
        _handlers.erase(handler->GetDescriptor());
    }

protected:
    void OnRead(ClientSocket* clinet, unsigned char* data, size_t size);

protected:
    unsigned int PacketThread();
    static unsigned int __stdcall __ThreadProc(void* that);

private:
    bool _connected;
    GUID _guid;
    std::map<int, IPacketHandler*> _handlers;
    std::list<BasePacket*> _packets;
    std::vector<unsigned char> _recv_data;
    Dispatcher& _disp;
    ClientSocket _client;
    threading::Locker _lock;
};

struct GUIDLessComparer {
    bool operator()(const GUID& left, const GUID& right) const
    {
        return std::memcmp(&left, &right, sizeof(GUID)) < 0;
    }
};

class ServerPacketManager
{
public:
    ServerPacketManager(Dispatcher& disp);

    void StartPassive();

    void Send(BasePacket* pkt);

    void AddHandler(IPacketHandler* handler)
    {
        assert(_handlers.find(handler->GetDescriptor()) == _handlers.cend());
        _handlers[handler->GetDescriptor()] = handler;
    }

    void RemoveHandler(IPacketHandler* handler)
    {
        assert(_handlers.find(handler->GetDescriptor()) != _handlers.cend());
        _handlers.erase(handler->GetDescriptor());
    }

    void AddClient(ClientSocket* client);
    void RemoveClient(ClientSocket* client);

    void CloseLocal(const GUID& guid, int cfd);

protected:
    void OnRead(ClientSocket* clinet, unsigned char* data, size_t size);

protected:
    unsigned int PacketThread();
    static unsigned int __stdcall __ThreadProc(void* that);

private:
    std::map<int, IPacketHandler*> _handlers;
    std::list<BasePacket*> _packets;
    std::map<ClientSocket*, std::vector<unsigned char>> _recv_data;
    Dispatcher& _disp;
    std::multimap<GUID, ClientSocket*, GUIDLessComparer> _clients;
    threading::Locker _lock;
};

}