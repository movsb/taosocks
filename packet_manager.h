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
    int             __cmd;
    unsigned int    __seq;
};

struct RelayPacket : BasePacket
{
    unsigned char data[1];

    static RelayPacket* Create(const unsigned char* data, size_t size)
    {
        auto pkt_size = sizeof(BasePacket) + size;
        auto p = new (new char[pkt_size]) RelayPacket;
        p->__size = pkt_size;
        p->__cmd = PacketCommand::Relay;
        std::memcpy(p->data, data, size);
        return p;
    }
};

struct ConnectPacket : BasePacket
{
    char host[256];
    char service[32];

    void revert()
    {
        for(int i = 0; i < sizeof(host); i += 2) {
            host[i] ^= 0xff;
        }
    }
};

struct ConnectRespondPacket : BasePacket
{
    int code;
    unsigned long  addr;
    unsigned short port;
};

struct DisconnectPacket : BasePacket
{

};

#pragma pack(pop)

struct IClientPacketHandler
{
    virtual void OnPacket(BasePacket* packet) = 0;
};

class ClientPacketManager
{
public:
    ClientPacketManager();

    void Read();
    void Send(BasePacket* pkt);

    std::function<void()> OnPacketSent;
    std::function<bool(BasePacket*)> OnPacketRead;
    std::function<void()> OnError;

protected:
    void _OnRead(unsigned char* data, size_t size);
    void _OnWrite();
    void _OnConnect(bool connected);
    void _OnClose(CloseReason reason);

protected:
    void _Connect();

private:
    unsigned int _seq;
    GUID _guid;
    BasePacket* _packet;
    DataWindow _recv_data;
    ClientSocket _worker;
};

struct GUIDLessComparer {
    bool operator()(const GUID& left, const GUID& right) const
    {
        return std::memcmp(&left, &right, sizeof(GUID)) < 0;
    }
};

struct IPacketHandler
{
    virtual GUID GetId() = 0;
    virtual void OnPacket(BasePacket* packet) = 0;
};

class ServerPacketManager
{
public:
    ServerPacketManager();

    void Send(BasePacket* pkt);

    std::function<void(ClientSocket* client, ConnectPacket* pkt)> OnNew;

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
    std::map<GUID, IPacketHandler*, GUIDLessComparer> _handlers;
    std::list<BasePacket*> _packets;
    std::map<ClientSocket*, DataWindow> _recv_data;
    std::map<GUID, std::vector<ClientSocket*>, GUIDLessComparer> _clients;
};

}