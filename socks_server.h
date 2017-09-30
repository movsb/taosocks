#pragma once

#include <vector>

#include "client_socket.h"
#include "packet_manager.h"

namespace taosocks {
namespace {

struct SocksVersion
{
    enum Value {
        v4 = 0x04,
        v5 = 0x05,
    };
};

struct SocksCommand
{
    enum Value {
        Stream      = 0x01,
    };
};

struct ConnectionStatus
{
    enum Value {
        Success     = 0x5A,
        Fail        = 0x5B,
    };
};

}

#pragma pack(push,1)

struct ResolveAndConnectPacket : BasePacket
{
    char host[256];
    char service[32];
};

struct ResolveAndConnectRespondPacket : BasePacket
{
    unsigned long  addr;
    unsigned short port;
    bool status;

    static ResolveAndConnectRespondPacket* Create(unsigned long addr, unsigned short port, bool status)
    {
        auto p = new ResolveAndConnectRespondPacket;
        p->__size = sizeof(ResolveAndConnectRespondPacket);
        p->__cmd = PacketCommand::Connect;

        p->addr = addr;
        p->port = port;
        p->status = status;

        return p;
    }

};

#pragma pack(pop)

class SocksServer : public IPacketHandler
{
private:
    struct Phrase
    {
        enum Value {
            Init,
            Command,
            Port,
            Addr,
            Domain,
            User,
            Finish,
        };
    };

public:
    SocksServer(ClientPacketManager& pktmgr, ClientSocket* client);

    struct ConnectionInfo
    {
        unsigned long addr;
        unsigned short port;
        int cfd;
        int sfd;
        ClientSocket* client;
    };

    typedef std::function<void(ConnectionInfo&)> OnSucceededT;
    typedef std::function<void(ConnectionInfo&)> OnFailedT;

    void OnSucceeded(OnSucceededT callback)
    {
        _onSucceeded = callback;
    }

    void OnFailed(OnFailedT callback)
    {
        _onFailed = callback;
    }

public:
    void feed(const unsigned char* data, size_t size);

    void finish();

protected:
    ClientPacketManager& _pktmgr;
    SocksVersion::Value _ver;
    bool _is_v4a;
    Phrase::Value _phrase;
    ClientSocket* _client;
    std::vector<unsigned char> _recv;
    unsigned short _port;
    in_addr _addr;
    std::string _domain;
    OnSucceededT _onSucceeded;
    OnFailedT _onFailed;

    // Inherited via IPacketHandler
    virtual int GetDescriptor() override { return _client->GetDescriptor(); }
    virtual void OnPacket(BasePacket* packet) override;

private:
    void OnResolveAndConnectRespondPacket(ResolveAndConnectRespondPacket* pkt);
};

}