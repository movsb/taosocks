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

struct SocksInfo
{
    std::string host;
    unsigned short port;
};

#pragma pack(push,1)

struct ResolveAndConnectPacket : BasePacket
{
    char host[256];
    char service[32];

    static ResolveAndConnectPacket* Create(const std::string& host, const std::string& service)
    {
        auto p = new ResolveAndConnectPacket;
        p->__cmd = PacketCommand::ResolveAndConnect;
        p->__size = sizeof(ResolveAndConnectPacket);

        assert(host.size() > 0 && host.size() < _countof(p->host));
        assert(service.size() > 0 && service.size() < _countof(p->service));

        std::strcpy(p->host, host.c_str());
        std::strcpy(p->service, service.c_str());

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
    SocksServer(PacketManager& pktmgr, ClientSocket* client);
    void OnSucceeded(std::function<void(const SocksInfo& info)> callback)
    {
        _onSucceeded = callback;
    }

public:
    void feed(const unsigned char* data, size_t size);

    void finish();

protected:
    PacketManager& _pktmgr;
    std::function<void(const SocksInfo& info)> _onSucceeded;
    SocksVersion::Value _ver;
    bool _is_v4a;
    Phrase::Value _phrase;
    ClientSocket* _client;
    std::vector<unsigned char> _recv;
    unsigned short _port;
    in_addr _addr;
    std::string _domain;

    // Inherited via IPacketHandler
    virtual int GetDescriptor() override { return _client->GetDescriptor(); }
    virtual void OnPacket(packet_manager::BasePacket* packet) override;
};

}