#include <algorithm>

#include "socks_server.h"

namespace taosocks {

SocksServer::SocksServer(threading::Dispatcher& disp, ClientSocket * client)
    : _client(client)
    , _phrase(Phrase::Init)
{
    _client->OnRead([&](ClientSocket*, unsigned char* data, size_t size) {
        feed(data, size);
        if(_phrase == Phrase::Finish) {
            finish();
        }
    });

    _client->OnWritten([&](ClientSocket*, size_t size) {

    });

    _client->OnClosed([&](ClientSocket*) {
        //delete this;
    });
}
void SocksServer::feed(const unsigned char * data, size_t size)
{
    _recv.insert(_recv.cend(), data, data + size);

    auto& D = _recv;

    while(!D.empty()) {
        switch(_phrase) {
        case Phrase::Init:
        {
            _ver = (SocksVersion::Value)D[0];
            D.erase(D.begin());

            if(_ver != SocksVersion::v4) {
                assert(0);
            }

            _phrase = Phrase::Command;
            break;
        }
        case Phrase::Command:
        {
            auto cmd = (SocksCommand::Value)D[0];
            D.erase(D.begin());

            if(cmd != SocksCommand::Stream) {
                assert(0);
            }

            _phrase = Phrase::Port;
            break;
        }
        case Phrase::Port:
        {
            if(D.size() < 2) {
                return;
            }

            unsigned short port_net = D[0] + (D[1] << 8);
            D.erase(D.begin(), D.begin() + 2);
            _port = ::ntohs(port_net);

            _phrase = Phrase::Addr;
            break;
        }
        case Phrase::Addr:
        {
            if(D.size() < 4) {
                return;
            }

            _addr.S_un.S_un_b.s_b1 = D[0];
            _addr.S_un.S_un_b.s_b2 = D[1];
            _addr.S_un.S_un_b.s_b3 = D[2];
            _addr.S_un.S_un_b.s_b4 = D[3];
            D.erase(D.begin(), D.begin() + 4);

            _phrase = Phrase::User;
            break;
        }
        case Phrase::User:
        {
            auto term = std::find_if(D.cbegin(), D.cend(), [](const unsigned char& c) {
                return c == '\0';
            });

            if(term == D.cend()) {
                return;
            }

            D.erase(D.begin(), term + 1);

            _is_v4a = _addr.S_un.S_un_b.s_b1 == 0
                && _addr.S_un.S_un_b.s_b2 == 0
                && _addr.S_un.S_un_b.s_b3 == 0
                && _addr.S_un.S_un_b.s_b4 != 0;

            _phrase = _is_v4a ? Phrase::Domain : Phrase::Finish;

            break;
        }
        case Phrase::Domain:
        {
            auto term = std::find_if(D.cbegin(), D.cend(), [](const unsigned char& c) {
                return c == '\0';
            });

            if(term == D.cend()) {
                return;
            }

            _domain = (char*)&D[0];

            D.erase(D.begin(), term + 1);

            _phrase = Phrase::Finish;

            break;
        }
        }
    }
}

void SocksServer::finish()
{
    if(_is_v4a) {
        resolver rsv;
        if(!rsv.resolve(_domain, std::to_string(_port))) {
            assert(0);
        }

        _addr.S_un.S_addr = rsv[0];
    }

    auto c = new ClientSocket(_disp);
    _onCreateRelayer(c);
    c->OnConnected([&, c](ClientSocket*) {
        _client->OnRead([&, c](ClientSocket*, unsigned char* data, size_t size) {
            c->Write(data, size, nullptr);
        });

        std::vector<unsigned char> data;
        data.push_back(0x00);
        data.push_back(ConnectionStatus::Success);

        if(_is_v4a) {
            data.push_back(_port >> 8);
            data.push_back(_port & 0xff);

            auto addr = _addr.S_un.S_addr;
            char* a = (char*)&addr;
            data.push_back(a[0]);
            data.push_back(a[1]);
            data.push_back(a[2]);
            data.push_back(a[3]);
        }
        else {
            data.push_back(0);
            data.push_back(0);
            data.push_back(0);
            data.push_back(0);
            data.push_back(0);
            data.push_back(0);
        }

        _client->Write(data.data(), data.size(), nullptr);

        c->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
            _client->Write(data, size, nullptr);
        });

        c->OnWritten([this](ClientSocket*, size_t size) {
        });

        c->OnClosed([this](ClientSocket*) {
            _client->Close();
        });

        c->_Read();
    });

    c->Connect(_addr, _port);
}
}

