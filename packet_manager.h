#pragma once

#include <list>
#include <string>
#include <map>
#include <vector>

#include "client_socket.h"
#include "thread_dispatcher.h"

namespace taosocks {
namespace packet_manager {

struct Command {
    enum Value {
        ResolveAndConnect,
    };
};
}

typedef packet_manager::Command PacketCommand;

namespace packet_manager {

#pragma pack(push, 1)

struct  BasePacket
{
    size_t __size;
    int __cmd;
    int __sfd;
    int __cfd;
};

#pragma pack(pop)

struct IPacketHandler
{
    virtual int GetDescriptor() = 0;
    virtual void OnPacket(BasePacket* packet) = 0;
};

class PacketManager
{
public:
    PacketManager(Dispatcher& disp);

    void Start();
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
    void OnRead(unsigned char* data, size_t size);

protected:
    unsigned int PacketThread();
    static unsigned int __stdcall __ThreadProc(void* that);

private:
    bool _connected;
    std::map<int, IPacketHandler*> _handlers;
    std::list<BasePacket*> _packets;
    std::vector<unsigned char> _recv_data;
    Dispatcher& _disp;
    ClientSocket _client;
    threading::Locker _lock;
};

}

using packet_manager::PacketManager;
using packet_manager::IPacketHandler;
using packet_manager::BasePacket;

}