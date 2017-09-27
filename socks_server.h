#pragma once

#include <vector>

#include "client_socket.h"
#include "thread_dispatcher.h"

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


class SocksServer
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
    SocksServer(threading::Dispatcher& disp, ClientSocket* client);
    void OnCreateRelayer(std::function<void(ClientSocket*)> callback)
    {
        _onCreateRelayer = callback;
    }

public:
    void feed(const unsigned char* data, size_t size);

    void finish();

protected:
    std::function<void(ClientSocket*)> _onCreateRelayer;
    SocksVersion::Value _ver;
    bool _is_v4a;
    Phrase::Value _phrase;
    ClientSocket* _client;
    std::vector<unsigned char> _recv;
    unsigned short _port;
    in_addr _addr;
    std::string _domain;
    threading::Dispatcher _disp;
};

}