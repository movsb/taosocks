#pragma once

#include <vector>
#include <exception>

#include "data_window.hpp"
#include "client_socket.h"
#include "packet_manager.h"

namespace taosocks {
namespace {

struct SocksVersion
{
    enum Value {
        v4 = 0x04,
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

struct ConnectPacket : BasePacket
{
    char host[256];
    char service[32];
};

struct ConnectRespondPacket : BasePacket
{
    int code;
    unsigned long  addr;
    unsigned short port;
};

#pragma pack(pop)

class SocksServer : public IPacketHandler
{
public:
    struct ConnectionInfo
    {
        unsigned long addr;
        unsigned short port;
        int cid;
        int sid;
        ClientSocket* client;
    };

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

    std::function<void(ConnectionInfo&)> OnSucceed;
    std::function<void(const std::string&)> OnError;

protected:
    void feed(const unsigned char* data, size_t size);
    void finish();

protected:
    void _OnClientClose(CloseReason::Value reason);
    void _OnClientRead(unsigned char* data, size_t size);

protected:
    ClientPacketManager& _pktmgr;
    SocksVersion::Value _ver;
    bool _is_v4a;
    Phrase::Value _phrase;
    ClientSocket* _client;
    DataWindow _recv;
    unsigned short _port;
    in_addr _addr;
    std::string _domain;

    virtual int GetId() override { return _client->GetId(); }
    virtual void OnPacket(BasePacket* packet) override;

private:
    void OnConnectPacket(ConnectRespondPacket* pkt);
};

}