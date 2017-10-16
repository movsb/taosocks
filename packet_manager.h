#pragma once

#include <list>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

#include "data_window.hpp"
#include "client_socket.h"
#include "thread_dispatcher.h"
#include "iocp_model.h"

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
    int             __size;
    GUID            __guid;
    unsigned int    __seq;
    int             __cmd;
    int             __sid;
    int             __cid;
};

struct RelayPacket : BasePacket
{
    unsigned char data[1];

    static RelayPacket* Create(SOCKET sid, SOCKET cid, const unsigned char* data, size_t size)
    {
        auto pkt_size = sizeof(BasePacket) + size;
        auto p = new (new char[pkt_size]) RelayPacket;
        p->__size = pkt_size;
        p->__cmd = PacketCommand::Relay;
        p->__sid = sid;
        p->__cid = cid;
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
    virtual int GetId() = 0;
    virtual void OnPacket(BasePacket* packet) = 0;
};

class ClientPacketManager
{
public:
    ClientPacketManager(IOCP& ios, Dispatcher& disp);

    void StartActive();

    const std::vector<ClientSocket*>& GetClients() { return _clients; }

    void Send(BasePacket* pkt);

    void AddHandler(IPacketHandler* handler)
    {
        assert(_handlers.find(handler->GetId()) == _handlers.cend());
        _handlers[handler->GetId()] = handler;
    }

    void RemoveHandler(IPacketHandler* handler)
    {
        assert(_handlers.find(handler->GetId()) != _handlers.cend());
        _handlers.erase(handler->GetId());
    }

protected:
    void Schedule();
    void OnRead(ClientSocket* clinet, unsigned char* data, size_t size);

private:
    unsigned int _seq;
    GUID _guid;
    std::map<int, IPacketHandler*> _handlers;
    std::list<BasePacket*> _packets;
    std::map<ClientSocket*, DataWindow> _recv_data;
    IOCP& _ios;
    Dispatcher& _disp;
    std::vector<ClientSocket*> _clients;
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
    ServerPacketManager(IOCP& ios, Dispatcher& disp);

    void StartPassive();

    void Send(BasePacket* pkt);

    void AddHandler(IPacketHandler* handler)
    {
        assert(_handlers.find(handler->GetId()) == _handlers.cend());
        _handlers[handler->GetId()] = handler;
    }

    void RemoveHandler(IPacketHandler* handler)
    {
        assert(_handlers.find(handler->GetId()) != _handlers.cend());
        _handlers.erase(handler->GetId());
    }

    void AddClient(ClientSocket* client);
    void RemoveClient(ClientSocket* client);

protected:
    void Schedule();
    void OnRead(ClientSocket* clinet, unsigned char* data, size_t size);

private:
    unsigned int _seq;
    std::map<int, IPacketHandler*> _handlers;
    std::list<BasePacket*> _packets;
    std::map<ClientSocket*, DataWindow> _recv_data;
    IOCP& _ios;
    Dispatcher& _disp;
    std::map<GUID, std::vector<ClientSocket*>, GUIDLessComparer> _clients;
};

}