#include <algorithm>

#include "socks_server.h"

#include "log.h"

namespace taosocks {

SocksServer::SocksServer(ClientPacketManager& pktmgr, ClientSocket * client)
    : _client(client)
    , _pktmgr(pktmgr)
    , _phrase(Phrase::Init)
{
    assert(_client != nullptr);

    _pktmgr.AddHandler(this);

    _client->OnRead([this](ClientSocket*, unsigned char* data, size_t size) { return _OnClientRead(data, size); });
    _client->OnWrite([this](ClientSocket*, size_t) {});
    _client->OnClose([this](ClientSocket*, CloseReason::Value reason) {return _OnClientClose(reason); });
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
                throw "Bad socks version.";
            }

            _phrase = Phrase::Command;
            break;
        }
        case Phrase::Command:
        {
            auto cmd = (SocksCommand::Value)D[0];
            D.erase(D.begin());

            if(cmd != SocksCommand::Stream) {
                throw "Not supported socks command.";
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
    auto p = new ConnectPacket;
    p->__cmd = PacketCommand::Connect;
    p->__size = sizeof(ConnectPacket);
    p->__sfd = (int)INVALID_SOCKET;
    p->__cfd = (int)_client->GetDescriptor();

    auto& host = _domain;
    auto service = std::to_string(_port);

    assert(host.size() > 0 && host.size() < _countof(p->host));
    assert(service.size() > 0 && service.size() < _countof(p->service));

    std::strcpy(p->host, host.c_str());
    std::strcpy(p->service, service.c_str());

    _pktmgr.Send(p);
}

void SocksServer::_OnClientClose(CloseReason::Value reason)
{
    if(reason = CloseReason::Actively) {
        LogLog("主动关闭连接");
    }
    else if(reason == CloseReason::Passively) {
        LogLog("浏览器断开了连接");
    }
    else if(reason == CloseReason::Reset) {
        LogLog("浏览器异常断开");
    }
}

void SocksServer::_OnClientRead(unsigned char * data, size_t size)
{
    try {
        feed(data, size);
    }
    catch(const std::string& e) {

    }

    if(_phrase == Phrase::Finish) {
        assert(_recv.empty());
        LogLog("解析完成");
        finish();
    }
}

void SocksServer::OnPacket(BasePacket* packet)
{
    if(packet->__cmd == PacketCommand::Connect) {
        OnConnectPacket(static_cast<ConnectRespondPacket*>(packet));
    }
    else {
        assert(0 && "invalid packet");
    }
}

void SocksServer::OnConnectPacket(ConnectRespondPacket* pkt)
{
    std::vector<unsigned char> data;
    data.push_back(0x00);
    data.push_back(pkt->code == 0 ? ConnectionStatus::Success : ConnectionStatus::Fail);

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

    auto ret = _client->Write(data.data(), data.size(), nullptr);
    LogLog("Socks应答状态：%d,%d", ret.Succ(), ret.Code());
    assert(ret.Succ());

    if(pkt->code == 0) {

        ConnectionInfo info;
        info.sfd = pkt->__sfd;
        info.cfd = pkt->__cfd;
        info.addr = pkt->addr;
        info.port = pkt->port;
        info.client = _client;
        assert(OnSucceed);
        _pktmgr.RemoveHandler(this);
        OnSucceed(info);
    }
    else {
        // 应该等到socks应答数据写完再关闭
        // 如果立即写成功，那么对方会主动关闭
        // Read 会报告被动关闭
        _client->Close();
        _pktmgr.RemoveHandler(this);
        assert(OnError);
        OnError("连接失败");
    }
}

}

