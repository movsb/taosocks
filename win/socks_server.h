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
        v4 = 0x04,  // v4 & v4a
        v5 = 0x05,
    };
};

struct AuthMethod
{
    enum Value {
        NoAuth      = 0x00,
        NotOffered  = 0xFF,
    };
};

struct SocksCommand
{
    enum Value {
        Stream      = 0x01,
    };
};

struct AddrType
{
    enum Value {
        IPv4        = 0x01,
        Domain      = 0x03,
    };
};

struct ConnectionStatus_v4
{
    enum Value {
        Success     = 0x5A,
        Fail        = 0x5B,
    };
};

struct ConnectionStatus_v5
{
    enum Value {
        Success     = 0x00,
        Fail        = 0x01,
    };
};

}

class SocksServer
{
public:
    struct ConnectionInfo
    {
        unsigned long addr;
        unsigned short port;
        int cid;
        int sid;
        ClientSocket* client;
        ClientPacketManager* pktmgr;
    };

private:
    struct Phrase
    {
        enum Value {
            Init,
            Init_v5_conn,
            AuthMethods,
            Command,
            Reserved_v5_1,
            AddrType,
            Addr_v5,
            Port,
            Addr,
            Domain,
            User,
            Finish,
        };
    };

public:
    SocksServer(ClientSocket* client);

    std::function<void(ConnectionInfo&)> OnSucceed;
    std::function<void(const std::string&)> OnError;

protected:
    void feed(const unsigned char* data, size_t size);
    void finish();

protected:
    void _OnClientClose(CloseReason reason);
    void _OnClientRead(unsigned char* data, size_t size);

protected:
    ClientPacketManager* _pktmgr;
    SocksVersion::Value _ver;
    bool _is_v4a;
    bool _is_v5;
    AddrType::Value _addr_type;
    Phrase::Value _phrase;
    ClientSocket* _client;
    DataWindow _recv;
    unsigned short _port;
    in_addr _addr;
    std::string _domain;

    bool OnPacket(BasePacket* packet);

private:
    void OnConnectPacket(ConnectRespondPacket* pkt);
};

}