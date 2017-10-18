#include <algorithm>

#include "socks_server.h"

#include "log.h"

namespace taosocks {

SocksServer::SocksServer(ClientSocket * client)
    : _client(client)
    , _phrase(Phrase::Init)
{
    assert(_client != nullptr);


    _client->Read();

    _client->OnRead([this](ClientSocket*, unsigned char* data, size_t size) { return _OnClientRead(data, size); });
    _client->OnWrite([this](ClientSocket*, size_t) {});
    _client->OnClose([this](ClientSocket*, CloseReason::Value reason) {return _OnClientClose(reason); });


    _pktmgr = new ClientPacketManager();

    _pktmgr->OnError = [this]() {
        OnError("无法连接到服务器。");
    };

    _pktmgr->OnPacketRead = [this](BasePacket* packet) {
        return OnPacket(packet);
    };

    _pktmgr->OnPacketSent = [this]() {

    };
}
void SocksServer::feed(const unsigned char * data, size_t size)
{
    if(_phrase == Phrase::Finish) {
        return;
        assert(0);
    }

    _recv.append(data, size);

    while(_recv.size() > 0) {
        switch(_phrase) {
        case Phrase::Init:
        {
            _ver = (SocksVersion::Value)_recv.get_byte();

            if(_ver != SocksVersion::v4) {
                throw "Bad socks version.";
            }

            _phrase = Phrase::Command;
            break;
        }
        case Phrase::Command:
        {
            auto cmd = (SocksCommand::Value)_recv.get_byte();

            if(cmd != SocksCommand::Stream) {
                throw "Not supported socks command.";
            }

            _phrase = Phrase::Port;
            break;
        }
        case Phrase::Port:
        {
            if(_recv.size() < 2) {
                return;
            }

            _port= ::ntohs(_recv.get_short());

            _phrase = Phrase::Addr;
            break;
        }
        case Phrase::Addr:
        {
            if(_recv.size() < 4) {
                return;
            }

            _addr.s_addr = _recv.get_int();

            _phrase = Phrase::User;
            break;
        }
        case Phrase::User:
        {
            auto index = _recv.index_of('\0');

            if(index == -1) {
                return;
            }

            _recv.get_string(index + 1, 1);

            _is_v4a = _addr.S_un.S_un_b.s_b1 == 0
                && _addr.S_un.S_un_b.s_b2 == 0
                && _addr.S_un.S_un_b.s_b3 == 0
                && _addr.S_un.S_un_b.s_b4 != 0;

            _phrase = _is_v4a ? Phrase::Domain : Phrase::Finish;

            break;
        }
        case Phrase::Domain:
        {
            auto index = _recv.index_of('\0');

            if(index == -1) {
                return;
            }

            _domain = _recv.get_string(index + 1, 1);

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
    p->__sid = -1;
    p->__cid = (int)_client->GetId();

    auto& host = _domain;
    auto service = std::to_string(_port);

    assert(host.size() > 0 && host.size() < _countof(p->host));
    assert(service.size() > 0 && service.size() < _countof(p->service));

    std::strcpy(p->host, host.c_str());
    std::strcpy(p->service, service.c_str());

    _pktmgr->Send(p);
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
    feed(data, size);

    if(_phrase == Phrase::Finish) {
        assert(_recv.size() == 0);
        LogLog("解析完成 cid=%d, domain=%s, port=%d", _client->GetId(), _domain.c_str(), _port);
        finish();
    }
    else {
        _client->Read();
    }
}

bool SocksServer::OnPacket(BasePacket* packet)
{
    if(packet->__cmd == PacketCommand::Connect) {
        OnConnectPacket(static_cast<ConnectRespondPacket*>(packet));
    }
    else {
        assert(0 && "invalid packet");
    }
    return false;
}

void SocksServer::OnConnectPacket(ConnectRespondPacket* pkt)
{
    DataWindow data;
    data.append(0x00);
    data.append(pkt->code == 0 ? ConnectionStatus::Success : ConnectionStatus::Fail);

    if(_is_v4a) {
        data.append(_port >> 8);
        data.append(_port & 0xff);

        auto addr = _addr.S_un.S_addr;
        char* a = (char*)&addr;
        data.append(a[0]);
        data.append(a[1]);
        data.append(a[2]);
        data.append(a[3]);
    }
    else {
        data.append(0);
        data.append(0);
        data.append(0);
        data.append(0);
        data.append(0);
        data.append(0);
    }

    _client->OnWrite([this, pkt](ClientSocket*, size_t) {
        if(pkt->code == 0) {
            ConnectionInfo info;
            info.sid = pkt->__sid;
            info.cid = pkt->__cid;
            info.addr = pkt->addr;
            info.port = pkt->port;
            info.client = _client;
            info.pktmgr = _pktmgr;
            assert(OnSucceed);
            OnSucceed(info);
        }
        else {
            // 应该等到socks应答数据写完再关闭
            // 如果立即写成功，那么对方会主动关闭
            // Read 会报告被动关闭
            _client->Close();
            assert(OnError);
            OnError("连接失败");
        }
    });

    auto ret = _client->Write((char*)data.data(), data.size(), nullptr);
    LogLog("Socks应答状态：%d,%d", ret.Succ(), ret.Code());
    assert(!ret.Fail());
}

}

